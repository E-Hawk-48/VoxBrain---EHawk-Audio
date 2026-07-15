# ============================================================
#  VoxBrain - Windows build script
#  Auto-detects Visual Studio 2022 OR 2026 (IDE or Build Tools)
#  via vswhere and uses the matching CMake generator.
# ============================================================
$ErrorActionPreference = "Continue"
Set-Location (Join-Path $PSScriptRoot "..")
$log = Join-Path (Get-Location) "build_log.txt"
"VoxBrain build started $(Get-Date)" | Out-File $log -Encoding utf8

function Fail($msg) {
    Write-Host ""
    Write-Host "BUILD FAILED: $msg" -ForegroundColor Red
    Write-Host "Full log: $log"
    exit 1
}

# ---- 1. Find CMake ------------------------------------------
$cmake = $null
$cmd = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmd) { $cmake = $cmd.Source }
if (-not $cmake -and (Test-Path "C:\Program Files\CMake\bin\cmake.exe")) {
    $cmake = "C:\Program Files\CMake\bin\cmake.exe"
}
if (-not $cmake) {
    Fail "CMake not found. Install from https://cmake.org/download/ (tick 'Add to PATH')."
}
Write-Host "Using CMake: $cmake"
"Using CMake: $cmake" | Out-File $log -Append -Encoding utf8

# ---- 2. Find Visual Studio via vswhere ----------------------
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Fail ("vswhere.exe not found - Visual Studio (or its Build Tools) is not installed.`n" +
          "Install 'Build Tools for Visual Studio' with the 'Desktop development with C++' workload.")
}

# Prefer an instance that actually has the C++ toolchain
$vsVersion = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationVersion 2>$null | Select-Object -First 1
$vsName = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property displayName 2>$null | Select-Object -First 1

if (-not $vsVersion) {
    # VS exists but has no C++ workload?
    $anyVs = & $vswhere -latest -products * -property displayName 2>$null | Select-Object -First 1
    if ($anyVs) {
        Write-Host "Found '$anyVs' but WITHOUT the C++ toolchain." -ForegroundColor Yellow
        Write-Host "Open 'Visual Studio Installer' > Modify > check 'Desktop development with C++' > Modify."
        Fail "C++ workload missing"
    }
    Fail ("No Visual Studio instance found.`n" +
          "Install 'Build Tools for Visual Studio' with the 'Desktop development with C++' workload.")
}

$major = [int]($vsVersion.Split('.')[0])
switch ($major) {
    17 { $generator = "Visual Studio 17 2022" }
    18 { $generator = "Visual Studio 18 2026" }
    default { Fail "Unsupported Visual Studio version: $vsVersion" }
}
Write-Host "Found: $vsName ($vsVersion)" -ForegroundColor Green
Write-Host "Using generator: $generator"
"VS: $vsName $vsVersion -> $generator" | Out-File $log -Append -Encoding utf8

# ---- 2b. Pre-download ONNX Runtime (robust, with retries) ----
$ortZip = "ThirdParty\onnxruntime-win-x64-1.20.1.zip"
$ortUrl = "https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip"
if (-not (Test-Path $ortZip)) {
    New-Item -ItemType Directory -Force -Path "ThirdParty" | Out-Null
    Write-Host ""
    Write-Host "Downloading ONNX Runtime (~60 MB, one time only)..." -ForegroundColor Cyan
    & curl.exe -L --retry 5 --retry-delay 3 -o $ortZip $ortUrl
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $ortZip)) {
        Remove-Item $ortZip -ErrorAction SilentlyContinue
        Fail "Could not download ONNX Runtime. Check your internet connection and retry."
    }
    $size = (Get-Item $ortZip).Length
    if ($size -lt 10000000) {   # sanity: a real download is ~60 MB
        Remove-Item $ortZip
        Fail "ONNX Runtime download was incomplete ($size bytes). Retry the build."
    }
}

# ---- 3. Configure -------------------------------------------
# Wipe a stale cache generated for a different VS version
if (Test-Path "build\CMakeCache.txt") {
    $cache = Get-Content "build\CMakeCache.txt" -Raw
    if ($cache -notmatch [regex]::Escape($generator)) {
        Write-Host "Clearing stale build cache from a previous attempt..."
        Remove-Item -Recurse -Force "build"
    }
}

Write-Host ""
Write-Host "[1/2] Configuring (first run downloads JUCE - takes a few minutes)..." -ForegroundColor Cyan
& $cmake -B build -G $generator -A x64 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { Fail "CMake configure step failed (see output above)" }

# ---- 4. Build ------------------------------------------------
Write-Host ""
Write-Host "[2/2] Compiling (several minutes on first build)..." -ForegroundColor Cyan
& $cmake --build build --config Release --parallel 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { Fail "Compile step failed (see output above)" }

# ---- 5. Install VST3 into the system folder ------------------
$vst3Src  = Join-Path (Get-Location) "build\VoxBrain_artefacts\Release\VST3\VoxBrain.vst3"
$vst3Dest = "C:\Program Files\Common Files\VST3"
$installed = $false
if (Test-Path $vst3Src) {
    try {
        Copy-Item $vst3Src $vst3Dest -Recurse -Force -ErrorAction Stop
        $installed = $true
    }
    catch {
        Write-Host ""
        Write-Host "Installing to the system VST3 folder needs admin - approve the prompt..." -ForegroundColor Yellow
        try {
            Start-Process powershell -Verb RunAs -Wait -ArgumentList `
                "-NoProfile", "-Command", "Copy-Item '$vst3Src' '$vst3Dest' -Recurse -Force"
            $installed = Test-Path (Join-Path $vst3Dest "VoxBrain.vst3")
        }
        catch { $installed = $false }
    }
}

Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host " BUILD SUCCEEDED" -ForegroundColor Green
Write-Host " VST3:       build\VoxBrain_artefacts\Release\VST3\VoxBrain.vst3"
Write-Host " Standalone: build\VoxBrain_artefacts\Release\Standalone\VoxBrain.exe"
if ($installed) {
    Write-Host " Installed to C:\Program Files\Common Files\VST3 - rescan plugins in your DAW." -ForegroundColor Green
} else {
    Write-Host " NOTE: could not auto-install to C:\Program Files\Common Files\VST3." -ForegroundColor Yellow
    Write-Host " Copy the .vst3 folder there manually (needs admin approval)."
}
Write-Host "============================================================" -ForegroundColor Green
exit 0
