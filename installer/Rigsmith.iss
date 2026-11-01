; Inno Setup script - installs Rigsmith.exe and the Rigsmith VST3.
; Built by CI:  ISCC.exe /DAppVersion=0.1.0 installer\Rigsmith.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{6B3A7F51-8E2D-4C1B-9B57-2E5C0D7A4F11}
AppName=Rigsmith
AppVersion={#AppVersion}
AppPublisher=Rigsmith
AppPublisherURL=https://github.com/brown111723/rigsmith
DefaultDirName={autopf}\Rigsmith
DefaultGroupName=Rigsmith
OutputDir=..\dist
OutputBaseFilename=Rigsmith-{#AppVersion}-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
UninstallDisplayIcon={app}\Rigsmith.exe

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "vst3"; Description: "Install the VST3 plug-in (Common Files\VST3)"; GroupDescription: "Plug-in:"

[Files]
Source: "..\build\Rigsmith_artefacts\Release\Standalone\Rigsmith.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Rigsmith_artefacts\Release\VST3\Rigsmith.vst3\*"; DestDir: "{commoncf64}\VST3\Rigsmith.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: vst3
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Dirs]
Name: "{userdocs}\Rigsmith\Presets"
Name: "{userdocs}\Rigsmith\Models"
Name: "{userdocs}\Rigsmith\IRs"

[Icons]
Name: "{group}\Rigsmith"; Filename: "{app}\Rigsmith.exe"
Name: "{group}\Uninstall Rigsmith"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Rigsmith"; Filename: "{app}\Rigsmith.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Rigsmith.exe"; Description: "Launch Rigsmith"; Flags: nowait postinstall skipifsilent
