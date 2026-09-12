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

[Files]
; The XDK payload is installed by the manifest engine at [Run] time (files,
; registry, shortcuts, shell extension) - not by Inno - so it can be placed
; exactly as the original installer (relocated to {app}). Here we only bundle
; our own tools + the VS integration.
Source: "unpacker\bin\Release\net472\RxdkXdkUnpacker.exe"; DestDir: "{app}\tools"
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
; The XDK's own Start-menu shortcuts are created (under the RXDK-360 group) by
; the manifest engine. Here we only add the uninstaller entry.
Name: "{group}\Uninstall RXDK-360"; Filename: "{uninstallexe}"

[Run]
; 1. Install the XDK itself, relocated to {app}: the manifest engine unpacks the
;    setup EXE and replays manifest.csv (files + registry + Start-menu shortcuts +
;    the Xbox 360 Neighborhood shell extension).
Filename: "{app}\tools\RxdkXdkUnpacker.exe"; \
  Parameters: "install ""{code:GetSetupExe}"" ""{app}"""; \
  StatusMsg: "Installing the Xbox 360 XDK (unpacking ~2 GB, this can take a few minutes)..."; \
  Flags: waituntilterminated
; 2. Wire up the modern-VS integration. The RXDK-360 platform reads XDKInstallDir
;    from the registry key written above, so it targets {app} automatically.
Filename: "powershell.exe"; \
  Parameters: "-ExecutionPolicy Bypass -NoProfile -File ""{app}\vsintegration\install.ps1"""; \
  StatusMsg: "Installing the Visual Studio integration..."; \
  Flags: runhidden waituntilterminated; Tasks: vs

[UninstallRun]
; Reverse the manifest install (files, registry, shortcuts, shell ext) first...
Filename: "{app}\tools\RxdkXdkUnpacker.exe"; Parameters: "uninstall ""{app}"""; \
  Flags: waituntilterminated; RunOnceId: "RxdkXdkUninstall"
; ...then remove the VS integration.
Filename: "powershell.exe"; \
  Parameters: "-ExecutionPolicy Bypass -NoProfile -File ""{app}\vsintegration\install.ps1"" -Uninstall"; \
  Flags: runhidden waituntilterminated; RunOnceId: "RxdkVsUninstall"

[Code]
var
  XDKPage: TInputFileWizardPage;
  XDKPageID: Integer;

procedure InitializeWizard();
begin
  XDKPage := CreateInputFilePage(wpWelcome,
    'Select the Xbox 360 XDK setup',
    'RXDK-360 relocates your own licensed XDK; nothing Microsoft is redistributed.',
    'Select your Xbox 360 XDK setup EXE (e.g. XDKSetupXenon<version>.exe), then click Next.');
  XDKPage.Add('&Location of the XDK setup EXE:', 'Executable files|*.exe|All files|*.*', '.exe');
  XDKPageID := XDKPage.ID;
end;

{ the selected setup EXE, passed to the manifest engine at [Run] time }
function GetSetupExe(Param: String): String;
begin
  Result := XDKPage.Values[0];
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = XDKPageID then
  begin
    if not FileExists(XDKPage.Values[0]) then
    begin
      MsgBox('Please select your Xbox 360 XDK setup EXE.', mbError, MB_OK);
      Result := False;
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
