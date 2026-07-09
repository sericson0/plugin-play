; Plugin Play — Inno Setup installer
; ---------------------------------------------------------------------------
; Builds a single signed-able Windows installer for the standalone app, and
; optionally downloads + runs VB-Audio's own VB-CABLE installer during setup so
; a non-technical DJ gets the virtual cable in one flow ("single simple download").
;
; Requires Inno Setup 6.1 or newer (for CreateDownloadPage / DownloadTemporaryFile).
; Build the Release exe first, then compile this script:
;     cmake --build build --config Release
;     "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\PluginPlay.iss
; The .exe lands in installer\Output\.
;
; To code-sign, pass /DSignTool=... to ISCC and add SignTool= lines, or sign the
; produced installer + app afterwards with signtool.exe.

#define AppName        "Plugin Play"
#define AppVersion     "0.1.0"
#define AppPublisher   "Plugin Play"
#define AppExeName     "Plugin Play.exe"
#define AppId          "{{7B2C9F4E-3A1D-4E8B-9C6F-2D5A8E1B4C70}"

; Where the Release build put the exe (relative to this script).
#ifndef SourceDir
  #define SourceDir "..\build\PluginPlay_artefacts\Release"
#endif

; VB-Audio's own VB-CABLE driver pack (never re-hosted — fetched from VB-Audio).
#define VbCableZipUrl "https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack45.zip"
#define VbCableSetup  "VBCABLE_Setup_x64.exe"

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppSupportURL=https://vb-audio.com/Cable/
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
OutputDir=Output
OutputBaseFilename=PluginPlay-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; The app is 64-bit; VB-CABLE install needs elevation.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
LicenseFile=..\LICENSE
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "vbcable"; Description: "Download && install VB-CABLE virtual audio cable (recommended)"; GroupDescription: "Virtual audio cable:"

[Files]
Source: "{#SourceDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
var
  DownloadPage: TDownloadWizardPage;

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := True;
end;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), @OnDownloadProgress);
end;

// Download the VB-CABLE zip (if the user opted in) on the way out of the Ready page,
// so a network failure is reported before any files are copied. A failed/refused
// download is non-fatal: the app can still install VB-CABLE later via its own
// VIRTUAL CABLE button.
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if (CurPageID = wpReady) and WizardIsTaskSelected('vbcable') then
  begin
    DownloadPage.Clear;
    DownloadPage.Add('{#VbCableZipUrl}', 'VBCABLE_Driver_Pack.zip', '');
    DownloadPage.Show;
    try
      try
        DownloadPage.Download;
      except
        if MsgBox('VB-CABLE could not be downloaded (' + GetExceptionMessage + ').' + #13#10 +
                  'Continue installing Plugin Play without it? You can install the cable later ' +
                  'from the app''s VIRTUAL CABLE button.', mbConfirmation, MB_YESNO) = IDNO then
          Result := False;
      end;
    finally
      DownloadPage.Hide;
    end;
  end;
end;

// After Plugin Play's files are in place, extract and launch VB-Audio's installer.
// Our installer already runs elevated, so the child install inherits elevation.
procedure CurStepChanged(CurStep: TSetupStep);
var
  ZipPath, ExtractDir, SetupExe, PsArgs, AppData: String;
  ResultCode: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    // Drop a marker so the app re-shows its first-run walkthrough once after this
    // install/upgrade. The app deletes it after showing.
    AppData := ExpandConstant('{userappdata}\PluginPlay');
    ForceDirectories(AppData);
    SaveStringToFile(AppData + '\.show-welcome', '', False);
  end;

  if (CurStep = ssPostInstall) and WizardIsTaskSelected('vbcable') then
  begin
    ZipPath    := ExpandConstant('{tmp}\VBCABLE_Driver_Pack.zip');
    ExtractDir := ExpandConstant('{tmp}\vbcable');

    if not FileExists(ZipPath) then
      Exit;   // download was skipped/failed; nothing to install

    // Unzip with PowerShell (present on all supported Windows versions).
    PsArgs := '-NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath ''' +
              ZipPath + ''' -DestinationPath ''' + ExtractDir + ''' -Force"';
    Exec('powershell.exe', PsArgs, '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

    SetupExe := ExtractDir + '\{#VbCableSetup}';
    if FileExists(SetupExe) then
      Exec(SetupExe, '', ExtractDir, SW_SHOW, ewWaitUntilTerminated, ResultCode)
    else
      MsgBox('VB-CABLE was downloaded but its installer could not be located after extraction.' + #13#10 +
             'You can install it later from the app (VIRTUAL CABLE button).', mbInformation, MB_OK);
  end;
end;
