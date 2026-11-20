#ifndef AppVersion
  #define AppVersion "1.0.4"
#endif
#ifndef SourceDir
  #define SourceDir "."
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{6F3A1C52-5B0E-4C2A-9D4E-7A1B2C3D4E5F}
AppName=VRG
AppVersion={#AppVersion}
AppPublisher=VRG
AppPublisherURL=https://vrgclient.app
DefaultDirName={autopf}\VRG
DefaultGroupName=VRG
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#OutputDir}
OutputBaseFilename=VRG-Setup
SetupIconFile=vrg.ico
UninstallDisplayIcon={app}\VRG.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\VRG.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\yt-dlp.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\VRG"; Filename: "{app}\VRG.exe"
Name: "{autodesktop}\VRG"; Filename: "{app}\VRG.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\VRG.exe"; Description: "{cm:LaunchProgram,VRG}"; Flags: nowait postinstall skipifsilent
