# VoxBrain updates — via GitHub Releases

VoxBrain auto-updates by reading the **latest GitHub Release** of your repo.
No web server or `appcast.json` to maintain — you just publish a Release with the
installers attached and installed copies pick it up.

The plugin reads (baked into the build, in `CMakeLists.txt`):

```
VOXBRAIN_UPDATE_URL = https://api.github.com/repos/E-Hawk-48/Custom-Vocal-Plugin/releases/latest
```

On load (≤ once every 4 h) it fetches that, compares the release's `tag_name`
(e.g. `v1.2.0`) to the running build's version, and if newer, silently downloads
the matching asset — the `.exe` on Windows, the `.pkg` on macOS — then shows the
"ready to install" banner. Requires the repo/Releases to be **public** (yours is).

## How to publish an update (fully automated)

1. Bump the version in `CMakeLists.txt`:
   `project(VoxBrain VERSION 1.2.0 …)`
2. Commit + push that.
3. Tag it and push the tag:
   ```
   git tag v1.2.0
   git push origin v1.2.0
   ```
4. That fires `.github/workflows/release.yml`, which on cloud runners builds the
   macOS `.pkg` **and** the Windows `.exe`, then creates the GitHub Release
   `v1.2.0` with both attached and auto-generated notes.
5. (Optional) Edit the release on GitHub to write nicer notes — whatever you put
   in the release body shows up in the plugin's "Release notes".

Done. Everyone running an older build sees the update within a few hours / on
next DAW launch.

### If the Windows build job ever fails

The macOS job and Windows job are independent, so a Windows failure still
publishes the release with the `.pkg`. To add the Windows installer by hand:
build locally (`Scripts\build_windows.bat`, then compile `installer\voxbrain.iss`
with Inno Setup) and drag the resulting `VoxBrain-Setup-1.2.0.exe` onto the
release page. I can also fix the CI job — send me the failing log.

## Important notes

- **The tag version must be higher than the version people already have**, or the
  updater (correctly) does nothing. Always bump `project(... VERSION …)` to match
  the tag.
- The very first GitHub-aware build has to be distributed once the normal way
  (the older builds you already shared point at a placeholder URL and never check).
  From then on, updates flow automatically.
- Keep the repo (and its Releases) **public** for this to work. If you make the
  source private, tell me — we'll point the updater at a separate public
  "releases" repo so your source stays private.

## Self-hosted fallback (not needed for GitHub)

`appcast.json` + `Scripts/publish_update.ps1` in this folder are the alternative
for hosting updates on your own website instead of GitHub. The plugin
auto-detects the format, so you can switch later by just changing
`VOXBRAIN_UPDATE_URL` back to your `appcast.json` URL.
