; WinSSH installer (Inno Setup 6). Per-user install, no admin rights needed.
;
; Built by scripts\package.ps1 (locally and in the release workflow):
;   ISCC.exe /DAppVersion=0.2.0 /DAppVersionNumeric=0.2.0 /DBuildDir=<build>\Release /DOutputDir=<dist> installer\WinSSH.iss

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef AppVersionNumeric
  #define AppVersionNumeric "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\build\dist"
#endif

#define AppName "WinSSH"
#define AppExe "WinSSH.exe"
#define AppUrl "https://github.com/winssh/WinSSH"

[Setup]
; Never change AppId: it links upgrades and the uninstaller to earlier versions.
AppId={{6E3A9C1B-2F4D-4B8E-9A7C-5D1E0F2B3C4A}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
VersionInfoVersion={#AppVersionNumeric}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ChangesEnvironment=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=WinSSH-setup-{#AppVersion}
SetupIconFile=..\assets\icons\WinSSH.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Tasks]
Name: "addtopath"; Description: "Add WinSSH to my PATH (run ""winssh"" from any terminal)"
Name: "wtprofiles"; Description: "Add WinSSH and my profiles to the Windows Terminal menu"

[Files]
Source: "{#BuildDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\highlight-rules.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\profiles.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[Icons]
Name: "{group}\WinSSH"; Filename: "{app}\{#AppExe}"; Comment: "SSH terminal for Windows"
Name: "{group}\WinSSH documentation"; Filename: "{app}\README.md"

[Run]
; Writes the Windows Terminal fragment with this install's path (updates it on upgrade).
Filename: "{app}\{#AppExe}"; Parameters: "--wt-install"; Tasks: wtprofiles; Flags: runhidden waituntilterminated; StatusMsg: "Adding WinSSH to Windows Terminal..."
Filename: "{code:WtExe}"; Parameters: "-p ""WinSSH"""; Description: "Open WinSSH in Windows Terminal"; Tasks: wtprofiles; Check: HasWindowsTerminal; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "--wt-uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "WtUninstall"

; Settings, profiles, history (%APPDATA%\WinSSH) and saved passwords (Credential Manager) are kept
; on uninstall, like most apps; delete them by hand to remove everything.

[Code]
const
  EnvKey = 'Environment';

function WtExe(Param: string): string;
begin
  Result := ExpandConstant('{localappdata}\Microsoft\WindowsApps\wt.exe');
end;

function HasWindowsTerminal(): Boolean;
begin
  Result := FileExists(WtExe(''));
end;

function PathContains(PathValue, Dir: string): Boolean;
begin
  Result := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(PathValue) + ';') > 0;
end;

// The user PATH is usually REG_EXPAND_SZ (contains %VARS%): keep that type when writing it back.
procedure AddToPath(Dir: string);
var
  Value: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvKey, 'Path', Value) then
    Value := '';
  if PathContains(Value, Dir) then
    exit;
  if (Value <> '') and (Copy(Value, Length(Value), 1) <> ';') then
    Value := Value + ';';
  RegWriteExpandStringValue(HKEY_CURRENT_USER, EnvKey, 'Path', Value + Dir);
end;

procedure RemoveFromPath(Dir: string);
var
  Value: string;
  P: Integer;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvKey, 'Path', Value) then
    exit;
  Value := ';' + Value + ';';
  P := Pos(';' + Uppercase(Dir) + ';', Uppercase(Value));
  if P = 0 then
    exit;
  Delete(Value, P, Length(Dir) + 1);
  while Pos(';;', Value) > 0 do
    StringChangeEx(Value, ';;', ';', True);
  if Copy(Value, 1, 1) = ';' then
    Delete(Value, 1, 1);
  if Copy(Value, Length(Value), 1) = ';' then
    Delete(Value, Length(Value), 1);
  RegWriteExpandStringValue(HKEY_CURRENT_USER, EnvKey, 'Path', Value);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    AddToPath(ExpandConstant('{app}'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemoveFromPath(ExpandConstant('{app}'));
end;
