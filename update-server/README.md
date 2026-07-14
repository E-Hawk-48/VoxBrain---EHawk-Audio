# VocalForge update server

This folder is what you publish to **your website** so the plugin can find updates.

## How it works

Every time VocalForge loads (at most once every 4 hours), it quietly fetches
`appcast.json` from the URL baked into the build (`VOCALFORGE_UPDATE_URL` in
`CMakeLists.txt`). If the `version` in the manifest is newer than the running
build, it **silently downloads** the correct installer for the user's OS,
verifies its SHA-256, and shows an "Update ready" banner in the plugin. The user
clicks **Install**, which launches the installer; they close their DAW to finish.

A loaded plugin can never replace itself, so the installer does the actual
file replacement — this is the same model Sparkle/WinSparkle use.

## One-time setup

1. Pick a URL on your site, e.g. `https://ehawkaudio.com/vocalforge/appcast.json`.
2. Put that exact URL in `CMakeLists.txt` → `VOCALFORGE_UPDATE_URL`, then rebuild.
   (Until you do this, the updater is a no-op — it never phones home.)
3. Make sure the folder is served over **HTTPS** (required).

## Publishing an update

1. Bump `project(VocalForge VERSION x.y.z …)` in `CMakeLists.txt`.
2. Build: `Scripts\build_windows.bat`.
3. Make the Windows installer with Inno Setup: compile `installer\vocalforge.iss`
   (set the version), producing `VocalForge-Setup-x.y.z.exe`.
4. (Optional) Build the macOS `.pkg` — see `Scripts/package_macos.sh` or the
   GitHub Actions workflow `.github/workflows/build-macos.yml` (builds on a cloud
   Mac, so you don't need one).
5. Generate the manifest + hashes:
   ```powershell
   Scripts\publish_update.ps1 -Version 1.2.0 `
     -WinInstaller path\to\VocalForge-Setup-1.2.0.exe `
     -MacInstaller path\to\VocalForge-1.2.0.pkg `
     -BaseUrl "https://ehawkaudio.com/vocalforge" `
     -Notes "• Added chorus`n• Fixed de-esser" `
     -NotesUrl "https://ehawkaudio.com/vocalforge/changelog.html"
   ```
6. Upload to `https://ehawkaudio.com/vocalforge/`:
   - `appcast.json` (overwrite the old one)
   - `VocalForge-Setup-1.2.0.exe`
   - `VocalForge-1.2.0.pkg` (if you made one)

That's it — installed copies will notice within a few hours (or on next DAW launch).

## Manifest format (`appcast.json`)

```json
{
  "version": "1.2.0",
  "notes": "• line one\n• line two",
  "notesUrl": "https://.../changelog.html",
  "windows": { "url": "https://.../VocalForge-Setup-1.2.0.exe", "sha256": "…" },
  "macos":   { "url": "https://.../VocalForge-1.2.0.pkg",       "sha256": "…" }
}
```

Notes:
- Only `version` + one platform block are strictly required.
- Lowering `version` never downgrades users (the plugin only acts when the
  manifest version is strictly newer).
- The `sha256` is optional but recommended — the plugin refuses a download whose
  hash doesn't match.
