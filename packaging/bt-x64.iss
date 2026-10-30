#define AppName "BT Karaoke"
[Setup]
AppId={{B8D4492F-A929-4F3A-8487-7319A4525A08}
AppName={#AppName}
AppVersion=2.4.1
DefaultDirName={localappdata}\Programs\BT Karaoke
DefaultGroupName={#AppName}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\installer-output
OutputBaseFilename=BTKaraoke-2.4.1-x64-setup
LicenseFile=..\LICENSE
SetupIconFile=..\icon.ico
Compression=lzma2
SolidCompression=yes
[Files]
Source: "..\dist-windows\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Dirs]
Name: "{app}\Songs\KAR"
Name: "{app}\Songs\NCN\Song"
Name: "{app}\Songs\NCN\Lyrics"
Name: "{app}\Songs\NCN\Cursor"
Name: "{app}\SoundFonts"
[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\HandyKaraoke.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\HandyKaraoke.exe"; WorkingDir: "{app}"
[Run]
Filename: "{app}\HandyKaraoke.exe"; WorkingDir: "{app}"; Description: "Launch BT Karaoke"; Flags: nowait postinstall skipifsilent
