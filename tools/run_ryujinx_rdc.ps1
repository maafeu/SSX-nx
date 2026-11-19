# run_ryujinx_rdc.ps1 -- one emulator run with RenderDoc captures of Ryujinx's Vulkan back end (the method of the
# user's other ports: RenderDoc's Vulkan layer enabled in Ryujinx's environment only, and qrenderdoc running
# tools/rdc/rdc_trigger.py, which takes captures -Times seconds after it connects).
#   powershell -File tools/run_ryujinx_rdc.ps1 -Seconds 300 -Name rdc1 -Times "220,235"
# Same rules as run_ryujinx.ps1: no other Ryujinx running; stops only what it started. RenderDoc: $env:RENDERDOC_DIR,
# else C:\Program Files\RenderDoc. Captures: out\emu_runs\<Name>\capture_NN_frameN.rdc. MIT.
param([int]$Seconds = 300, [string]$Name = "rdc", [string]$Times = "220", [string]$Nsp = "", [string]$Data = "",
      [string]$Ryu = "")
$root = Split-Path -Parent $PSScriptRoot
$rdcDir = if ($env:RENDERDOC_DIR) { $env:RENDERDOC_DIR } else { "C:\Program Files\RenderDoc" }
if (-not $Nsp) { $Nsp = Join-Path $root "ssx_nx.nsp" }
if (-not $Data) { $Data = if ($env:SSX_RYU_DATA) { $env:SSX_RYU_DATA } else { Join-Path (Split-Path -Parent $root) "ryujinx-data" } }
if (-not $Ryu) { $Ryu = if ($env:SSX_RYUJINX) { $env:SSX_RYUJINX } else { Join-Path (Split-Path -Parent (Split-Path -Parent $root)) "Ryujinx\publish\Ryujinx.exe" } }
if (Get-Process -Name Ryujinx -ErrorAction SilentlyContinue) { Write-Output "a Ryujinx is running: not starting"; exit 2 }
$game = Join-Path $Data "sdcard\switch\ssx-android"
$out = Join-Path $root "out\emu_runs\$Name"
New-Item -ItemType Directory -Force $out | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $game "debug.log")
$env:VK_LAYER_PATH = $rdcDir
$env:VK_INSTANCE_LAYERS = "VK_LAYER_RENDERDOC_Capture"
$env:ENABLE_VULKAN_RENDERDOC_CAPTURE = "1"
$p = Start-Process -FilePath $Ryu -ArgumentList @("-r", "`"$Data`"", "`"$Nsp`"") -PassThru -WindowStyle Minimized `
     -RedirectStandardOutput (Join-Path $out "ryujinx_stdout.txt") -RedirectStandardError (Join-Path $out "ryujinx_stderr.txt")
Remove-Item Env:\VK_LAYER_PATH, Env:\VK_INSTANCE_LAYERS, Env:\ENABLE_VULKAN_RENDERDOC_CAPTURE
$env:RDC_OUT = $out
$env:RDC_TIMES = $Times
$q = Start-Process -FilePath (Join-Path $rdcDir "qrenderdoc.exe") -ArgumentList @("--python", "`"$(Join-Path $root 'tools\rdc\rdc_trigger.py')`"") `
     -PassThru -WindowStyle Minimized
Write-Output ("started Ryujinx pid {0} and qrenderdoc pid {1}" -f $p.Id, $q.Id)
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline -and -not $p.HasExited) { Start-Sleep -Seconds 2 }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Write-Output "stopped Ryujinx pid $($p.Id) after $Seconds s" }
Start-Sleep -Seconds 3
if (-not $q.HasExited) { Stop-Process -Id $q.Id -Force; Write-Output "stopped qrenderdoc pid $($q.Id)" }
$src = Join-Path $game "debug.log"; if (Test-Path $src) { Copy-Item $src (Join-Path $out "debug.log") -Force }
Get-Content (Join-Path $out "trigger.txt") -ErrorAction SilentlyContinue
