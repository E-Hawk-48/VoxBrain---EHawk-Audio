; ============================================================================
;  VocalForge Windows installer  —  Inno Setup 6+  (https://jrsoftware.org/)
;
;  Produces VocalForge-Setup-<version>.exe, which installs the VST3 into the
;  shared VST3 folder and the Standalone app into Program Files. This is the
;  file the in-plugin auto-updater downloads and runs.
;
;  Build it:
;    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DAppVersion=1.2.0 installer\vocalforge.iss
;  (Run Scripts\build_windows.bat first so the artefacts exist.)
; ============================================================================
#define AppName "VocalForge"
#define AppPublisher "EHawk Audio"

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

; Path to the CMake build artefacts. Override with /DArtefacts=... if your
; build folder differs (VS multi-config puts them under Release/).
#ifndef Artefacts
  #define Artefacts "..\build\VocalForge_artefacts\Release"
#endif

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppId={{B7C2E1A4-9F3D-4E62-8B1A-VOCALFORGE01}}
DefaultDirName={commonpf}\{#AppPublisher}\{#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=.
OutputBaseFilename=VocalForge-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
; VST3 bundle — the build already copies onnxruntime.dll + VocalForgeModels
; inside the .vst3, so recursing the folder captures everything it needs.
Source: "{#Artefacts}\VST3\VocalForge.vst3\*"; DestDir: "{commoncf64}\VST3\VocalForge.vst3"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

; Standalone app + its runtime files (onnxruntime.dll, VocalForgeModels).
Source: "{#Artefacts}\Standalone\*"; DestDir: "{app}"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\VocalForge.exe"

[Run]
Filename: "{app}\VocalForge.exe"; Description: "Launch VocalForge"; \
    Flags: nowait postinstall skipifsilent
