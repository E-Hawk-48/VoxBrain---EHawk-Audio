# VoxBrain - New Computer Migration Checklist

Everything you need to move VoxBrain development to a new Windows PC and keep
building/shipping updates. Work top to bottom.

---

## 1. Before you leave the OLD computer (5 min)

- [ ] Push any unsaved work to GitHub as a safety net. Open a terminal in the
      `VocalForge` folder (type `cmd` in the File Explorer address bar) and run:
  ```
  git add -A
  git commit -m "backup before migrating"
  git push origin HEAD
  ```
  (If it says "nothing to commit," you're already backed up.)
- [ ] Note your current version (open `CMakeLists.txt`, line 3).

---

## 2. Transfer from OLD -> NEW (copy the whole project folder)

The surest method is to copy the entire top folder to an external drive, USB, or
cloud (OneDrive / Google Drive), then onto the new PC.

- [ ] **First, delete the giant build folders** so the copy isn't huge (they are
      rebuilt automatically - deleting them loses nothing):
  - `...\1\VocalForge\build\`
  - any other `build\` folder inside the project
- [ ] Copy the whole **`D:\AI Custom VSTs\1\`** folder to the new PC. Make sure
      these came along - they are needed and some are NOT on GitHub:
  - [ ] `CLAUDE.md`  (the project notes - lives ABOVE the repo, so a git clone would miss it)
  - [ ] `VocalForge\`  including its hidden **`.git`** folder (your history + the GitHub link)
  - [ ] `VocalForge\Models\`  (the neural pitch model, if present)
  - [ ] `VocalForge\ThirdParty\`  (ONNX zip - optional; it re-downloads if missing)
- [ ] Optional - your saved themes / presets / UI settings:
      copy `C:\Users\<you>\AppData\Roaming\VoxBrain\` to the same path on the new PC.

> Code-only alternative: `git clone https://github.com/E-Hawk-48/Custom-Vocal-Plugin.git`
> - but that does NOT include `CLAUDE.md` or `Models\`, so copy those by hand.

---

## 3. Download + install on the NEW computer

- [ ] **Visual Studio 2026 Community** (free) - during install, tick the
      **"Desktop development with C++"** workload. This is the actual compiler; the
      build won't work without it.
- [ ] **CMake** (latest, "Windows x64 Installer") - on the install screen choose
      **"Add CMake to the system PATH."**
- [ ] **Git for Windows** - includes the credential helper that logs you into GitHub.
- [ ] **Ableton Live 12 Lite** (and authorize it) - your DAW for testing.
- [ ] Your **audio interface driver**.
- [ ] Optional: **Inno Setup** - only if you want to build the installer locally.
      You don't need it for normal updates (GitHub builds the installer in the cloud).
- [ ] A working **internet connection** for the first build (JUCE + ONNX Runtime
      download themselves the first time).

---

## 4. Set up Git + GitHub (so you can push updates)

- [ ] Set your identity (once), in a terminal:
  ```
  git config --global user.name  "E Hawk"
  git config --global user.email "ehawk42069@gmail.com"
  ```
- [ ] The first time you `git push`, a browser window asks you to sign in to
      GitHub - sign in to the account that owns **E-Hawk-48/Custom-Vocal-Plugin**.
      After that it's remembered.

---

## 5. Verify it builds

- [ ] Double-click **`Scripts\build_windows.bat`**. The FIRST build is slow - it
      downloads JUCE and ONNX Runtime. Later builds are fast.
- [ ] If it fails, open **`build_log.txt`** in the project folder and send it to me.
- [ ] `Scripts\diagnose.bat` writes **`diag.txt`** - an environment report that
      confirms Visual Studio and CMake were found. Handy if anything's off.
- [ ] Load VoxBrain in Ableton and run **LEARN** to confirm it works end to end.

---

## 6. Keep working with me (Claude / Cowork)

- [ ] Install the Claude desktop app, open Cowork, and **select the copied
      `D:\AI Custom VSTs\1\` folder** so I have access to the project again.

---

## Notes / gotchas

- **TASCAM US-122:** its driver will be just as unstable on the new PC if that PC
  is Windows 11. The ONNX fix stops the plugin from triggering it, but a modern
  audio interface is still worth it for reliable production.
- **Version numbering:** keep publishing versions ABOVE your highest git tag, or
  testers get stuck re-downloading the same update. `Scripts\release.bat` handles
  this and warns you; when in doubt, go one number higher than the last tag.
- **Shipping an update from the new PC:** same as before - `Scripts\release.bat`
  (or the manual `git tag vX.Y.Z && git push origin vX.Y.Z` after bumping the
  version in CMakeLists), then watch the build at
  github.com/E-Hawk-48/Custom-Vocal-Plugin/actions.
