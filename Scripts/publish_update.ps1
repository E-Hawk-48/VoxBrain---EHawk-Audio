<#
    publish_update.ps1 — generate appcast.json (+ SHA-256 hashes) for a release.

    Usage:
      Scripts\publish_update.ps1 -Version 1.2.0 `
        -WinInstaller "C:\path\VoxBrain-Setup-1.2.0.exe" `
        -MacInstaller "C:\path\VoxBrain-1.2.0.pkg" `      # optional
        -BaseUrl "https://voxbrainaudio.com/voxbrain" `
        -Notes "• Added chorus`n• Fixed de-esser" `
        -NotesUrl "https://voxbrainaudio.com/voxbrain/changelog.html"

    Then upload the printed files to your website. See update-server\README.md.
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$WinInstaller,
    [string]$MacInstaller = "",
    [string]$BaseUrl  = "https://YOUR-DOMAIN.example/voxbrain",
    [string]$Notes    = "",
    [string]$NotesUrl = "",
    [string]$OutFile  = "appcast.json"
)

function Sha256($path) { (Get-FileHash -Algorithm SHA256 -Path $path).Hash.ToLower() }

if (-not (Test-Path $WinInstaller)) { Write-Error "Windows installer not found: $WinInstaller"; exit 1 }

$BaseUrl = $BaseUrl.TrimEnd('/')
$winName = Split-Path $WinInstaller -Leaf

$cast = [ordered]@{
    version  = $Version
    notes    = $Notes
    notesUrl = $NotesUrl
    windows  = [ordered]@{ url = "$BaseUrl/$winName"; sha256 = (Sha256 $WinInstaller) }
}

if ($MacInstaller -ne "" -and (Test-Path $MacInstaller)) {
    $macName = Split-Path $MacInstaller -Leaf
    $cast.macos = [ordered]@{ url = "$BaseUrl/$macName"; sha256 = (Sha256 $MacInstaller) }
}

$cast | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $OutFile

Write-Host ""
Write-Host "Wrote $OutFile for VoxBrain $Version" -ForegroundColor Green
Write-Host ""
Write-Host "Upload these to $BaseUrl/ (over HTTPS):"
Write-Host "  - $OutFile   (must be reachable at your VOXBRAIN_UPDATE_URL)"
Write-Host "  - $winName"
if ($cast.macos) { Write-Host "  - $macName" }
Write-Host ""
Write-Host "Installed copies will pick it up within ~4 hours or on next DAW launch."
