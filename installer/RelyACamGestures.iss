; Rely-A-Cam-Gestures: the PC installer (Inno Setup 6.4+ / 7).
; Built by scripts/package.ps1, which passes AppVersion, Stage and OutDir.
;
; Installs the app per machine, registers its Windows 11 virtual camera (a COM DLL that Windows'
; Frame Server loads, so it must be registered for the machine), and records where it went so the
; Rely-A-Cam OBS plugin can start it. The two output ticks become the app's first-run settings.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Stage
  #define Stage "..\build\installer\stage"
#endif
#ifndef OutDir
  #define OutDir "..\build\installer"
#endif

#define AppKey "Software\Rely-A-Cam-Gestures"
#define AppExe "Rely-A-Cam-Gestures.exe"
#define CameraDll "Rely-A-Cam-Gestures-Camera.dll"

[Setup]
AppId={{25D0CC57-B76D-4664-9911-BAF574E8C911}
AppName=Rely-A-Cam-Gestures
AppVersion={#AppVersion}
AppVerName=Rely-A-Cam-Gestures {#AppVersion}
AppPublisher=Niccc2007
AppPublisherURL=https://github.com/uukjtisa/rely-a-cam-gestures
AppSupportURL=https://github.com/uukjtisa/rely-a-cam-gestures/issues
DefaultDirName={autopf}\Rely-A-Cam-Gestures
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutDir}
OutputBaseFilename=RelyACamGestures-Setup-{#AppVersion}
SetupIconFile=..\resources\icon.ico
UninstallDisplayIcon={app}\{#AppExe}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; The app may be running (in the tray, or started by the plugin): Restart Manager closes it first.
CloseApplications=yes
RestartApplications=no
UninstallDisplayName=Rely-A-Cam-Gestures
VersionInfoVersion={#AppVersion}

[Messages]
WelcomeLabel2=This installs Rely-A-Cam-Gestures: hand and face gestures that zoom, switch scenes, record and play memes on your camera.%n%nIt works on its own with a webcam, and with the Rely-A-Cam OBS plugin it adds gestures to your phone cameras.

[Tasks]
Name: "vcam"; Description: "Its own virtual camera, ""Rely-A-Cam Gestures"", for Zoom, Meet, Discord and browsers (Windows 11)"; GroupDescription: "When it runs on its own, send the picture to:"; Check: IsWindows11
Name: "obscam"; Description: "OBS Virtual Camera (needs OBS installed; OBS doesn't have to be running)"; GroupDescription: "When it runs on its own, send the picture to:"
Name: "desktopicon"; Description: "A desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#Stage}\*"; Excludes: "{#CameraDll}"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Frame Server keeps the camera DLL loaded while an app watches the camera: replaced at restart if so.
Source: "{#Stage}\{#CameraDll}"; DestDir: "{app}"; Flags: ignoreversion regserver restartreplace uninsrestartdelete

[Registry]
Root: HKLM; Subkey: "{#AppKey}"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "{#AppKey}"; ValueType: dword; ValueName: "OutVirtualCamera"; ValueData: "1"; Tasks: vcam
Root: HKLM; Subkey: "{#AppKey}"; ValueType: dword; ValueName: "OutVirtualCamera"; ValueData: "0"; Tasks: not vcam
Root: HKLM; Subkey: "{#AppKey}"; ValueType: dword; ValueName: "OutObsCamera"; ValueData: "1"; Tasks: obscam
Root: HKLM; Subkey: "{#AppKey}"; ValueType: dword; ValueName: "OutObsCamera"; ValueData: "0"; Tasks: not obscam

[Icons]
Name: "{autoprograms}\Rely-A-Cam-Gestures"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\Rely-A-Cam-Gestures"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Open Rely-A-Cam-Gestures now"; Flags: postinstall nowait skipifsilent runasoriginaluser

[Code]
function IsWindows11(): Boolean;
var
  V: TWindowsVersion;
begin
  GetWindowsVersionEx(V);
  Result := (V.Major > 10) or ((V.Major = 10) and (V.Build >= 22000));
end;
