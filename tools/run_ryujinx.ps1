# run_ryujinx.ps1 -- one emulator run of ssx_nx.nsp in Ryujinx (Ryubing), with this port's own data folder.
#   powershell -File tools/run_ryujinx.ps1 -Seconds 180 -Name emu1
# Refuses to start while any Ryujinx runs; stops only the process it started. The data folder (keys, firmware,
# sdcard/switch/ssx-android/ with the player's APK) and the emulator are outside the repository:
#   -Data  default ..\ryujinx-data (next to this repository), or $env:SSX_RYU_DATA
#   -Ryu   default $env:SSX_RYUJINX, else ..\..\Ryujinx\publish\Ryujinx.exe
# After the run: the game folder's debug.log / crash.log and the emulator's log go to out\emu_runs\<Name>. MIT.
param([int]$Seconds = 180, [string]$Name = "emu", [string]$Nsp = "", [string]$Data = "", [string]$Ryu = "",
      [double]$MinCommitGB = 2, [switch]$KeepLog, [int]$Shots = 0)
# -Shots N: the window is left normal (not minimised) and grabbed every N seconds with PrintWindow
# (shot-NNN.png): what the emulator really shows, independent of the game's own read-back captures.
$root = Split-Path -Parent $PSScriptRoot
if (-not $Nsp) { $Nsp = Join-Path $root "ssx_nx.nsp" }
if (-not $Data) { $Data = if ($env:SSX_RYU_DATA) { $env:SSX_RYU_DATA } else { Join-Path (Split-Path -Parent $root) "ryujinx-data" } }
if (-not $Ryu) { $Ryu = if ($env:SSX_RYUJINX) { $env:SSX_RYUJINX } else { Join-Path (Split-Path -Parent (Split-Path -Parent $root)) "Ryujinx\publish\Ryujinx.exe" } }
foreach ($p in @($Nsp, $Data, $Ryu)) { if (-not (Test-Path $p)) { Write-Output "missing: $p"; exit 4 } }
if (Get-Process -Name Ryujinx -ErrorAction SilentlyContinue) { Write-Output "a Ryujinx is running (another session's?): not starting"; exit 2 }
$os = Get-CimInstance Win32_OperatingSystem
$freeCommit = $os.FreeVirtualMemory / 1MB
if ($freeCommit -lt $MinCommitGB) { Write-Output ("free commit {0:N1} GB < {1} GB: not starting" -f $freeCommit, $MinCommitGB); exit 3 }
$game = Join-Path $Data "sdcard\switch\ssx-android"
$out = Join-Path $root "out\emu_runs\$Name"
New-Item -ItemType Directory -Force $out | Out-Null
if (-not $KeepLog) { Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $game "debug.log"), (Join-Path $game "crash.log") }
$started = Get-Date
if ($Shots -gt 0) {
  Add-Type -AssemblyName System.Drawing
  Add-Type @"
using System; using System.Runtime.InteropServices;
public static class SsxWin {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
}
"@
}
$style = if ($Shots -gt 0) { "Normal" } else { "Minimized" }
$p = Start-Process -FilePath $Ryu -ArgumentList @("-r", "`"$Data`"", "`"$Nsp`"") -PassThru -WindowStyle $style `
     -RedirectStandardOutput (Join-Path $out "ryujinx_stdout.txt") -RedirectStandardError (Join-Path $out "ryujinx_stderr.txt")
Write-Output ("started Ryujinx pid {0} (free commit {1:N1} GB)" -f $p.Id, $freeCommit)
$deadline = (Get-Date).AddSeconds($Seconds)
$nextShot = (Get-Date).AddSeconds($Shots); $shot = 0
while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
  Start-Sleep -Seconds 1
  if ($Shots -gt 0 -and (Get-Date) -ge $nextShot) {
    $nextShot = (Get-Date).AddSeconds($Shots)
    $p.Refresh(); $h = $p.MainWindowHandle
    if ($h -ne [IntPtr]::Zero) {
      $r = New-Object SsxWin+RECT; [void][SsxWin]::GetClientRect($h, [ref]$r)
      $w = $r.R - $r.L; $hh = $r.B - $r.T
      if ($w -gt 0 -and $hh -gt 0) {
        $bmp = New-Object System.Drawing.Bitmap $w, $hh
        $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
        [void][SsxWin]::PrintWindow($h, $dc, 3)   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
        $g.ReleaseHdc($dc); $g.Dispose()
        $shot++; $bmp.Save((Join-Path $out ("shot-{0:D3}.png" -f $shot)), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
      }
    }
  }
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Write-Output "stopped pid $($p.Id) after $Seconds s" }
else { Write-Output "Ryujinx exited by itself (code $($p.ExitCode))" }
Start-Sleep -Seconds 2
foreach ($f in @("debug.log", "crash.log", "config.ini")) {
  $src = Join-Path $game $f; if (Test-Path $src) { Copy-Item $src (Join-Path $out $f) -Force }
}
$logdir = Join-Path $Data "Logs"
if (Test-Path $logdir) {
  Get-ChildItem $logdir -File | Where-Object { $_.LastWriteTime -ge $started } | Copy-Item -Destination $out -Force
}
Get-ChildItem $game -Include "*.png","*.bmp" -Recurse -Depth 1 -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $started } |
  Move-Item -Destination $out -Force
Get-ChildItem $out | Select-Object Name, Length | Format-Table -AutoSize | Out-String -Width 200
