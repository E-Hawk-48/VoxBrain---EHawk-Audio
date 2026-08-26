; ============================================================================
;  VoxBrain Windows installer  —  Inno Setup 6+  (https://jrsoftware.org/)
;
;  Produces VoxBrain-Setup-<version>.exe, which installs the VST3 into the
;  shared VST3 folder and the Standalone app into Program Files. This is the
;  file the in-plugin auto-updater downloads and runs.
;
;  Build it:
;    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DAppVersion=1.2.0 installer\voxbrain.iss
;  (Run Scripts\build_windows.bat first so the artefacts exist.)
; ============================================================================
#define AppName "VoxBrain"
#define AppPublisher "VoxBrain Audio"

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

; Path to the CMake build artefacts. Override with /DArtefacts=... if your
; build folder differs (VS multi-config puts them under Release/).
#ifndef Artefacts
  #define Artefacts "..\build\VoxBrain_artefacts\Release"
#endif

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppId={{B7C2E1A4-9F3D-4E62-8B1A-VOXBRAIN01}}
DefaultDirName={commonpf}\{#AppPublisher}\{#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=.
OutputBaseFilename=VoxBrain-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
; VST3 bundle — the build already copies onnxruntime.dll + VoxBrainModels
; inside the .vst3, so recursing the folder captures everything it needs.
Source: "{#Artefacts}\VST3\VoxBrain.vst3\*"; DestDir: "{commoncf64}\VST3\VoxBrain.vst3"; Excludes: "*.pdb"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

; Standalone app + its runtime files (onnxruntime.dll, VoxBrainModels).
; PDBs are excluded deliberately. The Release build emits debug symbols (see
; the /DEBUG block in CMakeLists, which exists so a field crash report names
; the function instead of a bare address), and VoxBrain.pdb alone is ~73 MB -
; it would nearly quadruple a 28 MB installer while being of no use on a
; user machine. The symbols stay in the build folder for reading crash.log.
Source: "{#Artefacts}\Standalone\*"; DestDir: "{app}"; Excludes: "*.pdb"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\VoxBrain.exe"

[Run]
Filename: "{app}\VoxBrain.exe"; Description: "Launch VoxBrain"; \
    Flags: nowait postinstall skipifsilent
