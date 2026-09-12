; SPDX-License-Identifier: GPL-3.0-or-later
; Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
;
; RXDK-360 installer (Inno Setup 6).
;
; Lays the Xbox 360 XDK down RELOCATED to C:\Program Files\RXDK-360 (from the
; user's own licensed XDK - either an existing stock install or the setup EXE),
; registers it under RXDK-360's own key + env var so it lives SIDE BY SIDE with a
; stock "Microsoft Xbox 360 SDK", and wires up the modern-VS integration
; (RXDK-360 MSBuild platform + task assemblies + project-template VSIX).
;
; Build this installer:  iscc RXDK-360.iss   (Inno Setup 6 / ISPP)
;
; NOTE: the XDK payload is NOT bundled (licensed Microsoft content). It is
; acquired at install time from the user's own XDK.

#define AppName    "RXDK-360"
#define AppVersion "0.1.0"
#define AppPublisher "Team Resurgent"
; Stable GUID -> used for upgrade detection + the uninstall entry.
#define AppId      "{{CBA0EA08-CFF0-4546-AF42-527BA322B49C}"

[Setup]
AppId={#AppId}
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
Uninstallable=yes
OutputBaseFilename=RXDK-360-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "envvar";  Description: "Set the RXDK360 environment variable (for the RXDK-360 command prompt)"; GroupDescription: "Integration:"
Name: "vs";      Description: "Install the Visual Studio integration (RXDK-360 platform + project templates)"; GroupDescription: "Integration:"
Name: "startmenu"; Description: "Create RXDK-360 Start menu shortcuts"; GroupDescription: "Integration:"

[Files]
; The RXDK-360 VS integration (ours) - bundled and copied to {app}\vsintegration.
Source: "..\Platforms\*";  DestDir: "{app}\vsintegration\Platforms"; Flags: recursesubdirs createallsubdirs; Excludes: "*.dll"
Source: "..\tasks\*";      DestDir: "{app}\vsintegration\tasks";     Flags: recursesubdirs createallsubdirs; Excludes: "\*\bin\*,\*\obj\*"
Source: "..\extension\*";  DestDir: "{app}\vsintegration\extension"; Flags: recursesubdirs createallsubdirs; Excludes: "\*\bin\*,\*\obj\*"
Source: "..\install.ps1";  DestDir: "{app}\vsintegration"
Source: "..\README.md";    DestDir: "{app}\vsintegration"
; NB: the XDK payload (headers/libs/tools) is NOT here - AcquireXdkPayload() in
; [Code] populates {app} from the user's own XDK at install time.

[Registry]
; RXDK-360's own SDK key (what the RXDK-360 MSBuild platform reads for
; XDKInstallDir - see PlatformToolsets\2010-01\Toolset.props). Kept separate from
; the stock HKLM\...\Xbox\2.0\SDK so the two SDKs coexist.
Root: HKLM; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "Version";     ValueData: "{#AppVersion}"
; Machine-wide RXDK360 env var (command-line tools). Task 'envvar'.
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: string; ValueName: "RXDK360"; ValueData: "{app}"; Flags: preservestringtype uninsdeletevalue; Tasks: envvar

[Icons]
Name: "{group}\RXDK-360 Command Prompt"; Filename: "{cmd}"; Parameters: "/k ""{app}\bin\win32\xdkvars.bat"""; Tasks: startmenu
Name: "{group}\Xbox Neighborhood";       Filename: "{app}\bin\win32\xbNeighborhood.exe"; Tasks: startmenu; Check: FileExists(ExpandConstant('{app}\bin\win32\xbNeighborhood.exe'))
Name: "{group}\PIX for Xbox";            Filename: "{app}\bin\win32\pix.exe";            Tasks: startmenu; Check: FileExists(ExpandConstant('{app}\bin\win32\pix.exe'))
Name: "{group}\RXDK-360 Documentation";  Filename: "{app}\doc\xdk.chm";                  Tasks: startmenu; Check: FileExists(ExpandConstant('{app}\doc\xdk.chm'))
Name: "{group}\Uninstall RXDK-360";      Filename: "{uninstallexe}"

[Run]
; Wire up the Visual Studio integration after files are in place. The RXDK-360
; platform reads XDKInstallDir from the registry key written above, so it targets
; {app} automatically. Requires the .NET SDK + a VS 2022/2026 install.
Filename: "powershell.exe"; \
  Parameters: "-ExecutionPolicy Bypass -NoProfile -File ""{app}\vsintegration\install.ps1"""; \
  StatusMsg: "Installing the Visual Studio integration..."; \
  Flags: runhidden waituntilterminated; Tasks: vs

[UninstallRun]
; Remove the VS integration (VSIX + platform folders) before files are deleted.
Filename: "powershell.exe"; \
  Parameters: "-ExecutionPolicy Bypass -NoProfile -File ""{app}\vsintegration\install.ps1"" -Uninstall"; \
  Flags: runhidden waituntilterminated; RunOnceId: "RxdkVsUninstall"

[Code]
var
  SourcePage: TInputFileWizardPage;

{ ---- prior-install detection: offer to remove an existing RXDK-360 first ---- }
function GetUninstallString(): String;
var
  key, s: String;
begin
  Result := '';
  key := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + '{#AppId}' + '_is1';
  if RegQueryStringValue(HKLM, key, 'UninstallString', s) then
    Result := s
  else if RegQueryStringValue(HKLM32, key, 'UninstallString', s) then
    Result := s;
end;

function InitializeSetup(): Boolean;
var
  uninst: String;
  code: Integer;
begin
  Result := True;
  uninst := GetUninstallString();
  if uninst <> '' then
  begin
    if MsgBox('A previous RXDK-360 install was found. Uninstall it first?',
              mbConfirmation, MB_YESNO) = IDYES then
    begin
      uninst := RemoveQuotes(uninst);
      Exec(uninst, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '',
           SW_HIDE, ewWaitUntilTerminated, code);
    end;
  end;
end;

{ ---- wizard: choose the XDK source (existing install path or the setup EXE) - }
procedure InitializeWizard();
begin
  SourcePage := CreateInputFilePage(wpSelectDir,
    'Xbox 360 XDK source',
    'RXDK-360 relocates your own licensed XDK; nothing Microsoft is redistributed.',
    'Select the source to install from. If you already have the Xbox 360 XDK ' +
    'installed, point at its folder; otherwise select the XDK setup EXE.');
  SourcePage.Add('XDK install folder OR setup EXE:', 'XDK setup|*.exe|All files|*.*', '.exe');
  { default to a detected stock install }
  SourcePage.Values[0] := GetEnv('XEDK');
end;

{ recursive copy helper (Inno has no built-in CopyTree) }
function CopyTree(const Src, Dst: String): Boolean;
var
  fr: TFindRec;
  s, d: String;
begin
  Result := ForceDirectories(Dst);
  if not Result then exit;
  if FindFirst(AddBackslash(Src) + '*', fr) then
  begin
    try
      repeat
        if (fr.Name = '.') or (fr.Name = '..') then continue;
        s := AddBackslash(Src) + fr.Name;
        d := AddBackslash(Dst) + fr.Name;
        if (fr.Attributes and $10) <> 0 then  { $10 = FILE_ATTRIBUTE_DIRECTORY }
        begin
          if not CopyTree(s, d) then begin Result := False; exit; end;
        end
        else
        begin
          if not FileCopy(s, d, False) then begin Result := False; exit; end;
        end;
      until not FindNext(fr);
    finally
      FindClose(fr);
    end;
  end;
end;

{ ---- payload acquisition -------------------------------------------------- }
{ Strategy A (implemented): copy an existing XDK folder tree to the target dir.
  Strategy B (setup EXE): staged extraction - see README; left as a shell-out
  point so the exact InstallShield switches can be pinned during testing. }
function AcquireXdkPayload(): Boolean;
var
  src: String;
begin
  Result := False;
  src := SourcePage.Values[0];
  if src = '' then
  begin
    MsgBox('No XDK source selected.', mbError, MB_OK);
    exit;
  end;

  if DirExists(src) then
  begin
    { copy the existing install tree to the target dir }
    Result := CopyTree(src, ExpandConstant('{app}'));
    if not Result then
      MsgBox('Failed to copy the XDK from: ' + src, mbError, MB_OK);
  end
  else if FileExists(src) then
  begin
    { setup EXE: TODO - run the installer to a staging dir then relocate.
      Placeholder so the flow is complete; refined once the InstallShield
      extract/silent switches are confirmed against the real EXE. }
    MsgBox('Installing directly from the setup EXE is not wired up yet in this ' +
           'scaffold. Please install the stock XDK first, then re-run and point ' +
           'at its folder.', mbInformation, MB_OK);
    Result := False;
  end
  else
    MsgBox('Source not found: ' + src, mbError, MB_OK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    if not AcquireXdkPayload() then
      Abort();  { stop the install if the payload could not be staged }
  end;
end;
