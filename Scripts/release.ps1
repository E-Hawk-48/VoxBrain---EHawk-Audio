<#
    release.ps1 — publish a VoxBrain update to your testers.

    What it does, in order:
      1. Reads the current version from CMakeLists.txt and suggests the next one.
      2. Bumps  project(VoxBrain VERSION x.y.z)  so older copies see the build
         as newer (required for auto-update to trigger).
      3. Commits your current code, creates a  vX.Y.Z  git tag, and pushes both.
      4. GitHub Actions then builds the Windows (.exe) + macOS (.pkg) installers
         and publishes a Release. Installed copies auto-update from it within
         ~4 hours or on the next DAW launch.

    Just double-click Scripts\release.bat and follow the prompts.
#>
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path $PSScriptRoot -Parent
Set-Location $repoRoot
$cmake = Join-Path $repoRoot "CMakeLists.txt"

if (-not (Test-Path $cmake)) { Write-Error "CMakeLists.txt not found next to Scripts\. Run this from the VoxBrain project."; exit 1 }

# --- current version ------------------------------------------------------
$content = [System.IO.File]::ReadAllText($cmake)
if ($content -notmatch 'project\(VoxBrain VERSION (\d+)\.(\d+)\.(\d+)') {
    Write-Error "Couldn't find 'project(VoxBrain VERSION x.y.z)' in CMakeLists.txt"; exit 1
}
$cur       = "$($Matches[1]).$($Matches[2]).$($Matches[3])"
$suggested = "$($Matches[1]).$($Matches[2]).$([int]$Matches[3] + 1)"

Write-Host ""
Write-Host "VoxBrain release" -ForegroundColor Cyan
Write-Host "  Current version: $cur"
$ver = Read-Host "  New version to publish [$suggested]"
if ([string]::IsNullOrWhiteSpace($ver)) { $ver = $suggested }
if ($ver -notmatch '^\d+\.\d+\.\d+$') { Write-Error "Version must look like 1.2.3"; exit 1 }

$notes = Read-Host "  One-line note for testers (optional)"

# --- sanity: git available + inside a repo + tag not taken ---------------
try { git rev-parse --is-inside-work-tree | Out-Null } catch { Write-Error "This folder is not a git repository, or git isn't installed."; exit 1 }
$existing = (git tag --list "v$ver")
if ($existing) { Write-Error "Tag v$ver already exists. Pick a higher version."; exit 1 }

# --- bump the version (no BOM, exact rewrite) ----------------------------
$new = $content -replace 'project\(VoxBrain VERSION \d+\.\d+\.\d+', "project(VoxBrain VERSION $ver"
[System.IO.File]::WriteAllText($cmake, $new)
Write-Host "  Bumped CMakeLists.txt to $ver" -ForegroundColor Green

# --- commit (if anything changed), tag, push ------------------------------
git add -A
git diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
    git commit -m "Release v$ver $notes" | Out-Null
    Write-Host "  Committed your current code."
} else {
    Write-Host "  Nothing new to commit (version bump only)."
    git commit --allow-empty -m "Release v$ver $notes" | Out-Null
}

git tag -a "v$ver" -m "$notes"
Write-Host "  Pushing to GitHub..."
git push origin HEAD
git push origin "v$ver"

Write-Host ""
Write-Host "Done. Tag v$ver pushed — GitHub is now building the installers." -ForegroundColor Green
Write-Host "  Watch the build:  https://github.com/E-Hawk-48/Custom-Vocal-Plugin/actions"
Write-Host "  Release appears:  https://github.com/E-Hawk-48/Custom-Vocal-Plugin/releases"
Write-Host "  Your testers' installed copies auto-update within ~4 hours or on next DAW launch."
Write-Host ""
