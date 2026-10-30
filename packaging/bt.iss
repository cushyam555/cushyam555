[Setup]
AppId={{372F0570-9E48-4372-BAE7-A9C40AB16832}
AppName=BT Karaoke Studio
AppVersion=0.4.0
DefaultDirName={localappdata}\Programs\BT Karaoke Studio
DefaultGroupName=BT Karaoke Studio
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
SetupIconFile=..\resources\bt.ico
OutputDir=..\installer-output
OutputBaseFilename=BTKaraoke-0.4.0-Windows-x64-setup
Compression=lzma2
SolidCompression=yes
[Files]
Source: "..\dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\BT Karaoke Studio"; Filename: "{app}\BTKaraoke.exe"
Name: "{group}\BT Karaoke Studio - Safe Mode"; Filename: "{app}\BTKaraoke.exe"; Parameters: "--safe-mode"
Name: "{userdesktop}\BT Karaoke Studio"; Filename: "{app}\BTKaraoke.exe"
[Run]
Filename: "{app}\BTKaraoke.exe"; Description: "Open BT Karaoke Studio"; Flags: nowait postinstall skipifsilent
