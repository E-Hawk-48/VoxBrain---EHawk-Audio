<#
    release.ps1 - publish a VoxBrain update to your testers.

    Steps it performs:
      1. Reads the current version from CMakeLists.txt (and your git tags) and
         suggests the next one.
      2. Bumps  project(VoxBrain VERSION x.y.z)  so older copies see the build as
         newer (required for auto-update to trigger).
      3. Commits your current code, creates a  vX.Y.Z  git tag, and pushes both.
      4. GitHub Actions then builds the Windows (.exe) + macOS (.pkg) installers
         and publishes a Release. Installed copies auto-update from it within
         about 4 hours or on the next DAW launch.

    Just double-click Scripts\release.bat and follow the prompts.

    NOTE: git prints normal progress to stderr. An earlier version of this script
    used ErrorActionPreference=Stop, which makes PowerShell treat that stderr
    output as a fatal error, so it failed even when git succeeded. This version
    checks git's real exit code instead. Keep this file pure ASCII - Windows
    PowerShell reads .ps1 as ANSI and chokes on fancy dashes/quotes.
#>
$ErrorActionPreference = 'Continue'
$ProgressPreference    = 'SilentlyContinue'

function Fail([string]$msg) {
    Write-Host ""
    Write-Host ("ERROR: " + $msg) -ForegroundColor Red
    Write-Host ""
    exit 1
}

# Run git, capturing stdout+stderr, and fail only on a non-zero exit code.
function Git-Run {
    param([string[]]$GitArgs, [switch]$AllowFail)
    $output = & git @GitArgs 2>&1 | Out-String
    $code = $LASTEXITCODE
    if ($code -ne 0 -and -not $AllowFail) {
        Fail ("git " + ($GitArgs -join ' ') + " failed:`r`n" + $output)
    }
    return @{ Code = $code; Text = $output }
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Fail "git is not installed or not on your PATH. Install 'Git for Windows', reopen this window, and try again."
}

$repoRoot = Split-Path $PSScriptRoot -Parent
Set-Location $repoRoot
$cmake = Join-Path $repoRoot 'CMakeLists.txt'
if (-not (Test-Path $cmake)) {
    Fail ("CMakeLists.txt not found in " + $repoRoot + " - run this from the VoxBrain project.")
}

# --- repo sanity ----------------------------------------------------------
$null = Git-Run @('rev-parse', '--is-inside-work-tree')
$remotes = Git-Run @('remote')
if ($remotes.Text.Trim() -eq '') {
    Fail "This repo has no git remote. Add one first with: git remote add origin YOUR-REPO-URL"
}

# --- current version ------------------------------------------------------
$content = [System.IO.File]::ReadAllText($cmake)
if ($content -notmatch 'project\(VoxBrain VERSION (\d+)\.(\d+)\.(\d+)') {
    Fail "Could not find 'project(VoxBrain VERSION x.y.z)' in CMakeLists.txt"
}
$cur = [version]("{0}.{1}.{2}" -f $Matches[1], $Matches[2], $Matches[3])

# Highest version already released as a tag.
$highest = $null
foreach ($t in ((Git-Run @('tag', '--list', 'v*')).Text -split "`r?`n")) {
    $t = $t.Trim()
    if ($t -match '^v(\d+\.\d+\.\d+)$') {
        $v = [version]$Matches[1]
        if (($null -eq $highest) -or ($v -gt $highest)) { $highest = $v }
    }
}

$base = $cur
if (($null -ne $highest) -and ($highest -gt $base)) { $base = $highest }
$suggested = "{0}.{1}.{2}" -f $base.Major, $base.Minor, ($base.Build + 1)

Write-Host ""
Write-Host "VoxBrain release" -ForegroundColor Cyan
Write-Host ("  Version in CMakeLists: " + $cur)
if ($null -ne $highest) { Write-Host ("  Latest released tag:   v" + $highest) }
if (($null -ne $highest) -and ($highest -gt $cur)) {
    Write-Host ""
    Write-Host ("  NOTE: CMakeLists (" + $cur + ") is BEHIND your latest tag (v" + $highest + ").") -ForegroundColor Yellow
    Write-Host  "  Those builds reported the old version, so testers can be stuck" -ForegroundColor Yellow
    Write-Host  "  re-downloading the same update. Publish a version ABOVE that tag." -ForegroundColor Yellow
    Write-Host ""
}
$ver = Read-Host ("  New version to publish [" + $suggested + "]")
if ([string]::IsNullOrWhiteSpace($ver)) { $ver = $suggested }
if ($ver -notmatch '^\d+\.\d+\.\d+$') { Fail "Version must look like 1.2.3" }

$notes = Read-Host "  One-line note for testers (optional)"
if ([string]::IsNullOrWhiteSpace($notes)) { $notes = "Update" }

# --- tag must be free -----------------------------------------------------
$existing = Git-Run @('tag', '--list', ("v" + $ver))
if ($existing.Text.Trim() -ne '') { Fail ("Tag v" + $ver + " already exists. Pick a higher version.") }

# --- bump the version (no BOM, exact rewrite) ----------------------------
$new = $content -replace 'project\(VoxBrain VERSION \d+\.\d+\.\d+', ("project(VoxBrain VERSION " + $ver)
[System.IO.File]::WriteAllText($cmake, $new)
Write-Host ("  Bumped CMakeLists.txt to " + $ver) -ForegroundColor Green

# --- commit ---------------------------------------------------------------
$null = Git-Run @('add', '-A')
$pending = Git-Run @('diff', '--cached', '--quiet') -AllowFail   # exit 1 = there ARE staged changes
if ($pending.Code -ne 0) {
    $null = Git-Run @('commit', '-m', ("Release v" + $ver + " - " + $notes))
    Write-Host "  Committed your current code."
} else {
    $null = Git-Run @('commit', '--allow-empty', '-m', ("Release v" + $ver + " - " + $notes))
    Write-Host "  Nothing new to commit; made a release marker commit."
}

# --- tag + push -----------------------------------------------------------
$null = Git-Run @('tag', '-a', ("v" + $ver), '-m', $notes)

$branch = (Git-Run @('rev-parse', '--abbrev-ref', 'HEAD')).Text.Trim()
if ([string]::IsNullOrWhiteSpace($branch) -or $branch -eq 'HEAD') { $branch = 'HEAD' }

Write-Host "  Pushing code to GitHub..."
$null = Git-Run @('push', 'origin', $branch)
Write-Host ("  Pushing tag v" + $ver + "...")
$null = Git-Run @('push', 'origin', ("v" + $ver))

Write-Host ""
Write-Host ("Done. Tag v" + $ver + " pushed - GitHub is now building the installers.") -ForegroundColor Green
Write-Host "  Watch the build:  https://github.com/E-Hawk-48/Custom-Vocal-Plugin/actions"
Write-Host "  Release appears:  https://github.com/E-Hawk-48/Custom-Vocal-Plugin/releases"
Write-Host "  Testers' installed copies auto-update within about 4 hours or on next DAW launch."
Write-Host ""
