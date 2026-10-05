; Windows installer for Chorda (Inno Setup 6).
; Built by CI: ISCC /DAppVersion=x.y.z packaging\windows\Chorda.iss
; Paths are relative to this file; the build output is in build\Chorda_artefacts\Release.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define Out "..\..\build\Chorda_artefacts\Release"

[Setup]
AppId={{6F1D2C0A-8E43-4B7E-9C51-2A7D0E3B9F14}
AppName=Chorda
AppVersion={#AppVersion}
AppPublisher=Roberto Chiurazzi
AppPublisherURL=https://github.com/robert0-ch1/Chorda
DefaultDirName={autopf}\Chorda
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\dist
OutputBaseFilename=Chorda-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Chorda
UninstallDisplayIcon={app}\Chorda.exe

[Types]
Name: "full";   Description: "VST3 plugin and standalone app"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 plugin (Ableton Live, Cubase, Bitwig, Reaper, FL Studio...)"; Types: full custom
Name: "standalone"; Description: "Standalone app"; Types: full custom

[Files]
Source: "{#Out}\VST3\Chorda.vst3\*"; DestDir: "{commoncf64}\VST3\Chorda.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Out}\Standalone\Chorda.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Chorda"; Filename: "{app}\Chorda.exe"; Components: standalone

[Run]
Filename: "{app}\Chorda.exe"; Description: "Open Chorda now"; Components: standalone; Flags: nowait postinstall skipifsilent unchecked
