#ifndef AppVersion
  #error AppVersion must be supplied by the packaging script.
#endif
#ifndef StageDir
  #error StageDir must be supplied by the packaging script.
#endif
#ifndef DistDir
  #error DistDir must be supplied by the packaging script.
#endif

[Setup]
AppId=io.github.rangerzy.chunfe
AppName=春风
AppVersion={#AppVersion}
AppPublisher=rangerzy
DefaultDirName={localappdata}\Programs\Chunfe
DefaultGroupName=春风
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#DistDir}
OutputBaseFilename=chunfe-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\Chunfe.exe
VersionInfoVersion={#AppVersion}

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\春风"; Filename: "{app}\Chunfe.exe"

[Run]
Filename: "{app}\Chunfe.exe"; Description: "启动春风"; Flags: nowait postinstall skipifsilent
