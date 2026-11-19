# rdc_run.ps1 -- runs one of the tools/rdc/rdc_*.py capture scripts inside qrenderdoc and waits for it (the user's
# other ports' script: RDC_IN / RDC_OUT / RDC_FIRST / RDC_LAST as the scripts read them; extra RDC_* variables may
# be set by the caller). RenderDoc: $env:RENDERDOC_DIR, else C:\Program Files\RenderDoc.
#   powershell -File tools/rdc_run.ps1 -Script rdc_draws.py -In out/emu_runs/rdc1/capture_01_frameN.rdc -Out out/rdc/a
param([string]$Script, [string]$In, [string]$Out, [int]$First = 0, [int]$Last = 1000000000, [int]$TimeoutS = 900)
$root = Split-Path -Parent $PSScriptRoot
$rdcDir = if ($env:RENDERDOC_DIR) { $env:RENDERDOC_DIR } else { "C:\Program Files\RenderDoc" }
$q = Join-Path $rdcDir "qrenderdoc.exe"
New-Item -ItemType Directory -Force $Out | Out-Null
$env:RDC_IN = (Resolve-Path $In).Path
$env:RDC_OUT = (Resolve-Path $Out).Path
$env:RDC_FIRST = "$First"
$env:RDC_LAST = "$Last"
$p = Start-Process -FilePath $q -ArgumentList @("--python", (Join-Path $root "tools\rdc\$Script")) -PassThru -WindowStyle Minimized
if (-not $p.WaitForExit($TimeoutS * 1000)) { Stop-Process -Id $p.Id -Force; Write-Output "timed out after $TimeoutS s" }
Get-ChildItem $env:RDC_OUT -Filter *.txt | ForEach-Object { Write-Output "== $($_.Name) ($((Get-Content $_.FullName).Count) lines)" }
