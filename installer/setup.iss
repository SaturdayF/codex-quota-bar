#ifndef AppVersion
  #define AppVersion "0.3.6"
#endif
#ifndef PayloadDir
  #define PayloadDir "..\artifacts\native-win-x64"
#endif

[Setup]
AppId=CodexQuotaBar
AppName=Codex Quota Bar
AppVersion={#AppVersion}
AppPublisher=SaturdayF
AppPublisherURL=https://github.com/SaturdayF/codex-quota-bar
DefaultDirName={localappdata}\Programs\CodexQuotaBar\{#AppVersion}
UsePreviousAppDir=no
DisableDirPage=no
DefaultGroupName=Codex Quota Bar
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\artifacts\releases\v{#AppVersion}
OutputBaseFilename=CodexQuotaBar-v{#AppVersion}-Setup
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\CodexQuotaBar.exe
WizardStyle=modern light hidebevels includetitlebar
Compression=lzma2/ultra64
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
CreateUninstallRegKey=not IsIsolatedTest

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimp"; MessagesFile: "ChineseSimplified.isl"

[LangOptions]
english.DialogFontName=Segoe UI
english.WelcomeFontName=Segoe UI
chinesesimp.DialogFontName=Microsoft YaHei UI
chinesesimp.WelcomeFontName=Microsoft YaHei UI

[Tasks]
Name: "desktopicon"; Description: "添加桌面快捷方式"; Flags: unchecked
Name: "loginstartup"; Description: "随 Windows 登录启动"; Flags: checkedonce

[Files]
Source: "{#PayloadDir}\CodexQuotaBar.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\version.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\LICENSE-json.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\uninstall-manifest.json"; DestDir: "{app}"; Flags: ignoreversion

[INI]
Filename: "{app}\installer.ini"; Section: "Installation"; Key: "Product"; String: "CodexQuotaBar"
Filename: "{app}\installer.ini"; Section: "Installation"; Key: "Version"; String: "{#AppVersion}"

[InstallDelete]
Type: files; Name: "{app}\uninstall.ps1"; Check: IsLegacyInstall

[Icons]
Name: "{code:DesktopShortcutFolder}\Codex Quota Bar"; Filename: "{app}\CodexQuotaBar.exe"; Parameters: "--settings"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{autoprograms}\Codex Quota Bar\Codex Quota Bar Settings"; Filename: "{app}\CodexQuotaBar.exe"; Parameters: "--settings"; Check: not IsIsolatedTest

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "CodexQuotaBar"; ValueData: """{app}\CodexQuotaBar.exe"""; Tasks: loginstartup; Flags: uninsdeletevalue; Check: not IsIsolatedTest

[Run]
Filename: "{app}\CodexQuotaBar.exe"; Description: "启动 Codex Quota Bar"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\installer.ini"
Type: filesandordirs; Name: "{localappdata}\CodexQuotaBar"; Check: ShouldRemoveSettings

[Code]
var RemoveSettings: Boolean;

function OpenEvent(Access: LongWord; Inherit: Boolean; Name: String): THandle;
  external 'OpenEventW@kernel32.dll stdcall';
function SetEvent(Handle: THandle): Boolean;
  external 'SetEvent@kernel32.dll stdcall';
function CloseHandle(Handle: THandle): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';
function CredDelete(TargetName: String; CredentialType, Flags: LongWord): Boolean;
  external 'CredDeleteW@advapi32.dll stdcall';

function HasSwitch(Name: String): Boolean;
var I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Name) = 0 then Result := True;
end;

function IsIsolatedTest: Boolean;
begin
  Result := HasSwitch('/ISOLATEDTEST=1');
end;

function DesktopShortcutFolder(Param: String): String;
begin
  if IsIsolatedTest then Result := ExpandConstant('{app}\test-desktop')
  else Result := ExpandConstant('{userdesktop}');
end;

procedure StopListener;
var Handle: THandle;
begin
  if IsIsolatedTest then Exit;
  Handle := OpenEvent(2, False, 'Local\CodexQuotaBar.Quit');
  if Handle <> 0 then begin SetEvent(Handle); CloseHandle(Handle); Sleep(2200); end;
end;

function IsLegacyInstall: Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\CodexQuotaBar.exe')) and
    FileExists(ExpandConstant('{app}\version.json')) and
    FileExists(ExpandConstant('{app}\uninstall.ps1'));
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var Dir: String; FindRec: TFindRec; HasFiles: Boolean;
begin
  Result := True;
  if CurPageID <> wpSelectDir then Exit;
  Dir := AddBackslash(ExpandFileName(WizardDirValue));
  if (Length(Dir) <= 3) or (CompareText(Dir, AddBackslash(ExpandConstant('{win}'))) = 0) or
     (CompareText(Dir, AddBackslash(GetEnv('USERPROFILE'))) = 0) then begin
    SuppressibleMsgBox('请选择本工具专用的文件夹，不能使用磁盘根目录或系统、用户根目录。', mbError, MB_OK, IDOK);
    Result := False; Exit;
  end;
  HasFiles := False;
  if FindFirst(Dir + '*', FindRec) then begin
    try
      repeat
        if (FindRec.Name <> '.') and (FindRec.Name <> '..') then HasFiles := True;
      until not FindNext(FindRec);
    finally FindClose(FindRec); end;
  end;
  if HasFiles and not (FileExists(Dir + 'CodexQuotaBar.exe') and FileExists(Dir + 'version.json')) then begin
    SuppressibleMsgBox('该文件夹已有其他内容。请选择空文件夹或已有的 Codex Quota Bar 安装目录。', mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopListener;
  Result := '';
end;

function ShouldRemoveSettings: Boolean;
begin
  Result := RemoveSettings and not IsIsolatedTest;
end;

function InitializeUninstall: Boolean;
begin
  RemoveSettings := HasSwitch('/RemoveSettings');
  if not UninstallSilent and not IsIsolatedTest then
    RemoveSettings := MsgBox('是否同时删除本工具的设置？不会影响 ChatGPT 或 Codex 的登录数据。', mbConfirmation, MB_YESNO) = IDYES;
  StopListener;
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usUninstall) and ShouldRemoveSettings then begin
    CredDelete('CodexQuotaBar.DeepSeek.ApiKey', 1, 0);
    CredDelete('CodexQuotaBar.DeepSeek.PlatformToken', 1, 0);
  end;
end;
