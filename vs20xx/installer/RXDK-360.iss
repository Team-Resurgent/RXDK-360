; SPDX-License-Identifier: GPL-3.0-or-later
; Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
;
; RXDK-360 installer (Inno Setup 6).
;
; Takes the user's own Xbox 360 XDK setup EXE, extracts it (7-Zip), and lays it
; down RELOCATED to C:\Program Files\RXDK-360, registered under RXDK-360's own
; key + env var so it lives SIDE BY SIDE with a stock "Microsoft Xbox 360 SDK".
; Then wires up the modern-VS integration (RXDK-360 MSBuild platform + task
; assemblies + project-template VSIX).
;
; The XDK payload is NOT bundled (licensed Microsoft content); it comes from the
; user's own setup EXE at install time.
;
; Build:  "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" RXDK-360.iss

#include "Uninstall.iss"

#define AppName    "RXDK-360"
#define AppVersion "0.1.0"
#define AppPublisher "Team Resurgent"

[Setup]
AppId=RXDK-360
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/Team-Resurgent/RXDK-360
DefaultDirName={commonpf}\RXDK-360
DefaultGroupName=RXDK-360
DisableProgramGroupPage=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
ChangesEnvironment=true
Uninstallable=yes
Compression=lzma2
SolidCompression=yes
OutputBaseFilename=RXDK-360-Setup
WizardStyle=modern
SetupIconFile=Icon.ico
WizardImageFile=WizardImage.bmp
WizardSmallImageFile=WizardSmallImage.bmp
MissingRunOnceIdsWarning=no

[Tasks]
Name: "envvar";    Description: "Set the RXDK360 environment variable"; GroupDescription: "Integration:"
Name: "vs";        Description: "Install the Visual Studio integration (RXDK-360 platform + project templates)"; GroupDescription: "Integration:"
Name: "startmenu"; Description: "Create RXDK-360 Start menu shortcuts"; GroupDescription: "Integration:"

[Files]
; --- the extracted XDK payload (external, from the temp extraction) -> {app} ---
; The Xbox 360 XDK setup extracts under an "XDK\" prefix; relocate the build
; essentials to {app}. (Add more subtrees here to install the full SDK.)
Source: "{tmp}\XDKTemp\XDK\bin\*";     DestDir: "{app}\bin";     Flags: external recursesubdirs createallsubdirs
Source: "{tmp}\XDKTemp\XDK\include\*"; DestDir: "{app}\include"; Flags: external recursesubdirs createallsubdirs
Source: "{tmp}\XDKTemp\XDK\lib\*";     DestDir: "{app}\lib";     Flags: external recursesubdirs createallsubdirs
Source: "{tmp}\XDKTemp\XDK\source\*";  DestDir: "{app}\source";  Flags: external recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{tmp}\XDKTemp\XDK\doc\*";     DestDir: "{app}\doc";     Flags: external recursesubdirs createallsubdirs skipifsourcedoesntexist

; --- bundled: the RXDK-360 XDK unpacker + the VS integration (ours) ---
Source: "unpacker\bin\Release\net472\RxdkXdkUnpacker.exe"; Flags: dontcopy
Source: "..\Platforms\*"; DestDir: "{app}\vsintegration\Platforms"; Flags: recursesubdirs createallsubdirs; Excludes: "*.dll"
Source: "..\tasks\*";     DestDir: "{app}\vsintegration\tasks";     Flags: recursesubdirs createallsubdirs; Excludes: "\*\bin\*,\*\obj\*"
Source: "..\extension\*"; DestDir: "{app}\vsintegration\extension"; Flags: recursesubdirs createallsubdirs; Excludes: "\*\bin\*,\*\obj\*"
Source: "..\install.ps1"; DestDir: "{app}\vsintegration"
Source: "..\README.md";   DestDir: "{app}\vsintegration"

[Registry]
; RXDK-360's own SDK key (read first by the RXDK-360 platform's Toolset.props),
; kept separate from the stock HKLM\...\Xbox\2.0\SDK so the two SDKs coexist.
Root: HKLM; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "Version";     ValueData: "{#AppVersion}"

[Icons]
Name: "{group}\RXDK-360 Command Prompt"; Filename: "{cmd}"; Parameters: "/k ""{app}\bin\win32\xdkvars.bat"""; Tasks: startmenu
Name: "{group}\Xbox Neighborhood";       Filename: "{app}\bin\win32\xbNeighborhood.exe"; Tasks: startmenu; Check: FileExists(ExpandConstant('{app}\bin\win32\xbNeighborhood.exe'))
Name: "{group}\PIX for Xbox";            Filename: "{app}\bin\win32\pix.exe";            Tasks: startmenu; Check: FileExists(ExpandConstant('{app}\bin\win32\pix.exe'))
Name: "{group}\Xbox Watson";             Filename: "{app}\bin\win32\xbwatson.exe";       Tasks: startmenu; Check: FileExists(ExpandConstant('{app}\bin\win32\xbwatson.exe'))
Name: "{group}\Uninstall RXDK-360";      Filename: "{uninstallexe}"

[Run]
; Wire up the VS integration. The RXDK-360 platform reads XDKInstallDir from the
; registry key written above, so it targets {app} automatically. Needs .NET SDK
; + a VS 2022/2026 install.
Filename: "powershell.exe"; \
  Parameters: "-ExecutionPolicy Bypass -NoProfile -File ""{app}\vsintegration\install.ps1"""; \
  StatusMsg: "Installing the Visual Studio integration..."; \
  Flags: runhidden waituntilterminated; Tasks: vs

[UninstallRun]
Filename: "powershell.exe"; \
  Parameters: "-ExecutionPolicy Bypass -NoProfile -File ""{app}\vsintegration\install.ps1"" -Uninstall"; \
  Flags: runhidden waituntilterminated; RunOnceId: "RxdkVsUninstall"

[Code]
var
  XDKPage: TInputFileWizardPage;
  XDKPageID: Integer;
  ExtractPage: TOutputMarqueeProgressWizardPage;
  ResultCode: Integer;

procedure InitializeWizard();
begin
  XDKPage := CreateInputFilePage(wpWelcome,
    'Select the Xbox 360 XDK setup',
    'RXDK-360 relocates your own licensed XDK; nothing Microsoft is redistributed.',
    'Select your Xbox 360 XDK setup EXE (e.g. XDKSetupXenon<version>.exe), then click Next.');
  XDKPage.Add('&Location of the XDK setup EXE:', 'Executable files|*.exe|All files|*.*', '.exe');
  XDKPageID := XDKPage.ID;
  ExtractPage := CreateOutputMarqueeProgressPage('Extracting the XDK', 'Please wait while the XDK is unpacked...');
end;

{ On leaving the file page, extract the XDK with 7-Zip into the temp dir; the
  external file entries above then relocate the XDK tree into the target. }
function NextButtonClick(CurPageID: Integer): Boolean;
var
  setupExe, tempDir: String;
begin
  Result := True;
  if CurPageID = XDKPageID then
  begin
    setupExe := XDKPage.Values[0];
    if not FileExists(setupExe) then
    begin
      MsgBox('Please select your Xbox 360 XDK setup EXE.', mbError, MB_OK);
      Result := False;
      exit;
    end;
    ExtractPage.Show;
    try
      ExtractPage.Animate;
      ExtractTemporaryFile('RxdkXdkUnpacker.exe');
      tempDir := ExpandConstant('{tmp}\XDKTemp');
      { our self-contained unpacker walks the setup's concatenated MS cabinets }
      if not Exec(ExpandConstant('{tmp}\RxdkXdkUnpacker.exe'),
                  '"' + setupExe + '" "' + tempDir + '"',
                  '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
      begin
        MsgBox('Failed to unpack the XDK setup.', mbError, MB_OK);
        Result := False;
      end
      else if not DirExists(tempDir + '\XDK\bin') then
      begin
        MsgBox('That does not look like an Xbox 360 XDK setup (no XDK\bin after extraction).', mbError, MB_OK);
        Result := False;
      end;
    finally
      ExtractPage.Hide;
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    { clean upgrade: remove a prior RXDK-360 before laying down the new one }
    if IsUpgrade('RXDK-360') then
      UnInstallOldVersion('RXDK-360');
  end
  else if CurStep = ssPostInstall then
  begin
    { machine-wide RXDK360 env var (task 'envvar'); Uninstall removes it }
    if WizardIsTaskSelected('envvar') then
      RegWriteExpandStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'RXDK360', ExpandConstant('{app}'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RegDeleteValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'RXDK360');
end;
