; OBS Privacy Guard 0.1.0 - установщик для стационарной OBS Studio.
; Только копирует DLL и локали в каталог OBS. Сцены, профили и настройки
; (%APPDATA%\obs-studio) не затрагиваются.

#define MyAppName "OBS Privacy Guard"
#define MyAppVersion "0.8.6"
#define SrcRoot "D:\PROJECT\OpenCode\OBS_Blur\release\RelWithDebInfo\obs-privacy-guard"

[Setup]
AppId={{B4C7A9E1-2F3A-4B5C-9D8E-0A1B2C3D4E50}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Privacy Guard
DefaultDirName={reg:HKLM64\SOFTWARE\OBS Studio,,C:\Program Files\obs-studio}
DisableDirPage=no
DirExistsWarning=no
AppendDefaultDirName=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=yes
CloseApplicationsFilter=obs64.exe,obs32.exe
OutputDir=D:\PROJECT\OpenCode\OBS_Blur\package
OutputBaseFilename=obs-privacy-guard-0.8.6-windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName}
CreateUninstallRegKey=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#SrcRoot}\bin\64bit\obs-privacy-guard.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "{#SrcRoot}\data\locale\ru-RU.ini"; DestDir: "{app}\data\obs-plugins\obs-privacy-guard\locale"; Flags: ignoreversion
Source: "{#SrcRoot}\data\locale\en-US.ini"; DestDir: "{app}\data\obs-plugins\obs-privacy-guard\locale"; Flags: ignoreversion
Source: "{#SrcRoot}\data\privacy-guard-blur.effect"; DestDir: "{app}\data\obs-plugins\obs-privacy-guard"; Flags: ignoreversion

[Code]
function InitializeSetup(): Boolean;
var
  ObsPath: String;
begin
  Result := True;
  if not RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', ObsPath) then
  begin
    MsgBox('OBS Studio не найдена в реестре. Проверьте, что OBS установлена, и укажите ее папку вручную на следующем шаге.',
      mbInformation, MB_OK);
  end;
end;

procedure InitializeWizard();
var
  ObsPath: String;
begin
  if WizardForm.DirEdit.Text = '' then
  begin
    if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', ObsPath) and (ObsPath <> '') then
      WizardForm.DirEdit.Text := ObsPath
    else
      WizardForm.DirEdit.Text := 'C:\Program Files\obs-studio';
  end;
end;
