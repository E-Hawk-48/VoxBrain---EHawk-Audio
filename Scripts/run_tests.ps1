<#
    run_tests.ps1 - build and run the VoxBrain test suite.

    What this does:
      1. Configures a SEPARATE build folder (build-tests) with tests enabled, so
         your normal plugin build is never touched or slowed down.
      2. Builds the test program.
      3. Runs every test and prints a plain PASS/FAIL summary.

    Run this after any change to the DSP or the AI brains. If everything says
    PASSED, the change did not break anything the tests cover. If something
    FAILS, the line tells you which check and what the measured value was.

    NOTE: git and MSBuild write normal progress to STDERR, so this script uses
    ErrorActionPreference='Continue' and checks real exit codes instead - the
    same lesson learned the hard way in release.ps1.
    Keep this file pure ASCII: Windows PowerShell reads .ps1 as ANSI.
#>
$ErrorActionPreference = 'Continue'
$ProgressPreference    = 'SilentlyContinue'

function Fail([string]$msg) {
    Write-Host ""
    Write-Host ("ERROR: " + $msg) -ForegroundColor Red
    Write-Host ""
    exit 1
}

$repoRoot = Split-Path $PSScriptRoot -Parent
Set-Location $repoRoot

# --- locate CMake ---------------------------------------------------------
$cmake = "cmake"
if (-not (Get-Command $cmake -ErrorAction SilentlyContinue)) {
    $guess = "C:\Program Files\CMake\bin\cmake.exe"
    if (Test-Path $guess) { $cmake = $guess }
    else { Fail "CMake not found. Install it and tick 'Add CMake to the system PATH'." }
}

# --- locate Visual Studio via vswhere (never hardcode the version) --------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$generator = $null
if (Test-Path $vswhere) {
    $vsInfo = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property catalog_productLineVersion
    if ($vsInfo) {
        switch ($vsInfo.Trim()) {
            "2022" { $generator = "Visual Studio 17 2022" }
            "2026" { $generator = "Visual Studio 18 2026" }
        }
    }
}
if (-not $generator) { $generator = "Visual Studio 18 2026" }

Write-Host ""
Write-Host "VoxBrain test suite" -ForegroundColor Cyan
Write-Host ("  Generator: " + $generator)
Write-Host ""

# --- configure ------------------------------------------------------------
Write-Host "Configuring (first run downloads JUCE and may take a few minutes)..."
& $cmake -S . -B build-tests -G $generator -A x64 -DVB_BUILD_TESTS=ON 2>&1 | Out-String | Write-Host
if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed (see the output above)." }

# --- build ----------------------------------------------------------------
Write-Host ""
Write-Host "Building the tests..."
& $cmake --build build-tests --config Release --target VoxBrainTests 2>&1 | Out-String | Write-Host
if ($LASTEXITCODE -ne 0) { Fail "The tests did not compile (see the output above)." }

# --- run ------------------------------------------------------------------
$exe = Join-Path $repoRoot "build-tests\Release\VoxBrainTests.exe"
if (-not (Test-Path $exe)) {
    $exe = Join-Path $repoRoot "build-tests\VoxBrainTests.exe"
}
if (-not (Test-Path $exe)) { Fail "Built, but the test program was not found where expected." }

Write-Host ""
Write-Host "Running tests..." -ForegroundColor Cyan
Write-Host ""
& $exe
$testExit = $LASTEXITCODE

Write-Host ""
if ($testExit -eq 0) {
    Write-Host "All tests passed." -ForegroundColor Green
} else {
    Write-Host "Some tests FAILED - see the list above." -ForegroundColor Red
    Write-Host "Each failure names the suite, the check, and the measured value."
}
Write-Host ""
exit $testExit
