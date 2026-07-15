# ============================================================
#  VoxBrain - packages the built VST3 into a shareable zip
#  with install instructions. Run via package_plugin.bat.
# ============================================================
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$bundle = "build\VoxBrain_artefacts\Release\VST3\VoxBrain.vst3"
$zipOut = "VoxBrain-v0.1.0-win64.zip"

if (-not (Test-Path $bundle)) {
    Write-Host "No built plugin found at $bundle" -ForegroundColor Red
    Write-Host "Run Scripts\build_windows.bat first."
    exit 1
}

# Sanity-check the bundle contents
$binDir = Join-Path $bundle "Contents\x86_64-win"
$checks = @(
    (Join-Path $binDir "VoxBrain.vst3"),
    (Join-Path $binDir "onnxruntime.dll"),
    (Join-Path $binDir "VoxBrainModels\crepe-tiny.onnx")
)
foreach ($c in $checks) {
    if (-not (Test-Path $c)) {
        Write-Host "MISSING from bundle: $c" -ForegroundColor Red
        Write-Host "Rebuild first (Scripts\build_windows.bat), then package again."
        exit 1
    }
}
Write-Host "Bundle check passed (plugin + ONNX runtime + CREPE model present)." -ForegroundColor Green

# Staging folder with instructions
$stage = "build\_package"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $bundle (Join-Path $stage "VoxBrain.vst3") -Recurse

@"
VoxBrain - AI Vocal Engineer (VST3, Windows 64-bit)
=====================================================

INSTALL
1. Copy the whole "VoxBrain.vst3" FOLDER (keep it intact!) into:
      C:\Program Files\Common Files\VST3\
   (Windows will ask for administrator permission - click Continue.)
2. Open your DAW and rescan plugins (or just restart the DAW).
3. Insert "VoxBrain" on a vocal track.

USE
- Click LEARN, play/sing the vocal for 10-30 seconds, click again.
  The AI analyzes the recording and builds the whole vocal chain,
  explaining every decision in the report panel.
- Or type instructions in the chat box: "darker", "more air",
  "hard tune", "less autotune", "warmer", "radio ready", "reset"...

REQUIREMENTS
- Windows 10/11, 64-bit DAW (FL Studio, Ableton, Reaper, Cubase,
  Studio One, Cakewalk/Sonar, Bitwig...)
- CPU from ~2014 or newer (AVX2)
- If the plugin does not appear after a rescan, install the free
  "Microsoft Visual C++ Redistributable (x64)" from:
  https://aka.ms/vs/17/release/vc_redist.x64.exe
"@ | Out-File (Join-Path $stage "INSTALL.txt") -Encoding utf8

# Zip it
if (Test-Path $zipOut) { Remove-Item $zipOut }
Compress-Archive -Path "$stage\*" -DestinationPath $zipOut
Remove-Item -Recurse -Force $stage

$size = [math]::Round((Get-Item $zipOut).Length / 1MB, 1)
Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host " PACKAGED: $zipOut ($size MB)" -ForegroundColor Green
Write-Host " Send this one file. It contains the plugin + INSTALL.txt." -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
