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

[Types]
Name: "full";   Description: "Full - legacy XDK toolchain + modern (Clang/LLVM) toolchain"
Name: "legacy"; Description: "Legacy only - stock XDK toolchain"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
; The relocated XDK and the VS integration always install (the modern toolchain
; also uses the XDK's import libraries). The modern component adds the self-
; contained Clang/LLVM toolchain, XexTool and xdvdfs so RXDK-360 needs no external
; toolchain and does not depend on an RXDK-Tools install.
Name: "modern"; Description: "Modern toolchain (Clang/LLVM + XexTool + xdvdfs, self-contained)"; Types: full

[Tasks]
Name: "envvar";    Description: "Set the RXDK360 environment variable"; GroupDescription: "Integration:"
Name: "vs";        Description: "Install the Visual Studio integration (RXDK-360 platform + project templates)"; GroupDescription: "Integration:"

[Files]
; The XDK payload is installed by the manifest engine at [Run] time (files,
; registry, shortcuts, shell extension) - not by Inno - so it can be placed
; exactly as the original installer (relocated to {app}). Here we only bundle
; our own tools + the VS integration.
Source: "unpacker\bin\Release\net472\RxdkXdkUnpacker.exe"; DestDir: "{app}\tools"
Source: "unpacker\bin\Release\net472\RxdkXdkUnpacker.exe"; Flags: dontcopy
Source: "..\Platforms\*"; DestDir: "{app}\vsintegration\Platforms"; Flags: recursesubdirs createallsubdirs; Excludes: "*.dll"
Source: "..\tasks\*";     DestDir: "{app}\vsintegration\tasks";     Flags: recursesubdirs createallsubdirs; Excludes: "\*\bin\*,\*\obj\*"
Source: "..\extension\*"; DestDir: "{app}\vsintegration\extension"; Flags: recursesubdirs createallsubdirs; Excludes: "\*\bin\*,\*\obj\*"
Source: "..\install.ps1"; DestDir: "{app}\vsintegration"
Source: "..\README.md";   DestDir: "{app}\vsintegration"

; --- Modern (Clang/LLVM) toolchain payload -------------------------------------
; Self-contained: our own Clang + ld.lld (only the two binaries we drive, plus
; clang's resource headers - not the full 775MB LLVM bin), the modern C/C++
; runtime archives and headers, XexTool, and xdvdfs. Laid out to mirror the dev
; tree under {app}\modern so the clang Toolset.props defaults resolve unchanged.
; Requires build/ to be populated (build the LLVM/runtime; drop xdvdfs in
; build\tools per build\tools\README.md) before compiling the installer.
Source: "..\..\build\llvm\bin\clang.exe";   DestDir: "{app}\modern\build\llvm\bin"; Components: modern
Source: "..\..\build\llvm\bin\ld.lld.exe";  DestDir: "{app}\modern\build\llvm\bin"; Components: modern
Source: "..\..\build\llvm\lib\clang\*";     DestDir: "{app}\modern\build\llvm\lib\clang"; Flags: recursesubdirs createallsubdirs; Components: modern
Source: "..\..\build\libc\*.a";             DestDir: "{app}\modern\build\libc"; Components: modern
Source: "..\..\build\coff\*.a";             DestDir: "{app}\modern\build\coff"; Components: modern
Source: "..\..\runtime\config\*";           DestDir: "{app}\modern\runtime\config"; Flags: recursesubdirs createallsubdirs; Components: modern
Source: "..\..\vendor\picolibc\libc\include\*";            DestDir: "{app}\modern\vendor\picolibc\libc\include"; Flags: recursesubdirs createallsubdirs; Components: modern
Source: "..\..\vendor\llvm-project\libcxx\include\*";      DestDir: "{app}\modern\vendor\llvm-project\libcxx\include"; Flags: recursesubdirs createallsubdirs; Components: modern
Source: "..\..\vendor\llvm-project\libcxxabi\include\*";   DestDir: "{app}\modern\vendor\llvm-project\libcxxabi\include"; Flags: recursesubdirs createallsubdirs; Components: modern
Source: "..\..\vendor\xextool\build\Release\XexTool.exe";  DestDir: "{app}\modern\tools"; Components: modern
Source: "..\..\build\tools\xdvdfs.exe";     DestDir: "{app}\modern\tools"; Components: modern

[Registry]
; RXDK-360's own SDK key (read first by the RXDK-360 platform's Toolset.props),
; kept separate from the stock HKLM\...\Xbox\2.0\SDK so the two SDKs coexist.
Root: HKLM; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "Version";     ValueData: "{#AppVersion}"
; Modern toolchain root: the clang Toolset.props + Platform.targets resolve the
; Clang/LLVM tools, runtime and xdvdfs from here (see ModernPath / tools\xdvdfs.exe).
Root: HKLM; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "ModernPath"; ValueData: "{app}\modern"; Components: modern; Flags: uninsdeletevalue

[Icons]
; The XDK's own Start-menu shortcuts are created (under the RXDK-360 group) by
; the manifest engine. Here we only add the uninstaller entry.
Name: "{group}\Uninstall RXDK-360"; Filename: "{uninstallexe}"

[Run]
; The XDK install itself (unpack + manifest replay) runs from [Code] with a live
; progress page (see DoManifestInstall). Here we only wire up the VS integration.
; The RXDK-360 platform reads XDKInstallDir from the registry key written above,
; so it targets {app} automatically.
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
  ProgressPage: TOutputProgressWizardPage;

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

{ the first run of digits after 'prefix' in s, or -1 }
function IntAfter(const s, prefix: String): Integer;
var p, i: Integer; num: String;
begin
  Result := -1;
  p := Pos(prefix, s);
  if p = 0 then exit;
  i := p + Length(prefix);
  num := '';
  while (i <= Length(s)) and (s[i] >= '0') and (s[i] <= '9') do begin num := num + s[i]; i := i + 1; end;
  if num <> '' then Result := StrToIntDef(num, -1);
end;

{ Run the manifest engine and tail its --progress file into a wizard progress
  page, so the unpack/install output shows in the wizard (not a console). }
procedure DoManifestInstall();
var
  progFile, s, lastLine, doneLine: String;
  lines: TArrayOfString;
  code, i, cab, files, pct: Integer;
  finished, started: Boolean;
begin
  ProgressPage := CreateOutputProgressPage('Installing the Xbox 360 XDK',
    'Unpacking your XDK and installing it, relocated to ' + ExpandConstant('{app}') + '.');
  ProgressPage.Show;
  try
    ProgressPage.SetProgress(0, 100);
    progFile := ExpandConstant('{tmp}\rxdk_progress.txt');
    DeleteFile(progFile);
    ExtractTemporaryFile('RxdkXdkUnpacker.exe');
    started := Exec(ExpandConstant('{tmp}\RxdkXdkUnpacker.exe'),
      'install --progress "' + progFile + '" "' + GetSetupExe('') + '" "' + ExpandConstant('{app}') + '"',
      '', SW_HIDE, ewNoWait, code);
    if not started then
    begin
      MsgBox('Failed to start the XDK unpacker.', mbError, MB_OK);
      exit;
    end;

    finished := False;
    while not finished do
    begin
      Sleep(400);
      if LoadStringsFromFile(progFile, lines) then
      begin
        for i := 0 to GetArrayLength(lines) - 1 do
        begin
          s := lines[i];
          if Pos('###DONE', s) = 1 then begin finished := True; doneLine := s; end
          else if s <> '' then lastLine := s;
        end;
        pct := 5;
        cab := IntAfter(lastLine, 'cab ');
        files := IntAfter(lastLine, 'installing files... ');
        if cab >= 0 then pct := 5 + (cab * 45) div 15
        else if files >= 0 then pct := 50 + (files * 45) div 6300
        else if Pos('applying manifest', lastLine) > 0 then pct := 50
        else if Pos('manifest:', lastLine) > 0 then pct := 98;
        if pct > 99 then pct := 99;
        ProgressPage.SetText('Installing the Xbox 360 XDK...', lastLine);
        ProgressPage.SetProgress(pct, 100);
      end;
    end;
    ProgressPage.SetProgress(100, 100);

    if Pos('###DONE 0', doneLine) <> 1 then
      MsgBox('The XDK install reported a problem:'#13#10 + lastLine, mbError, MB_OK);
  finally
    ProgressPage.Hide;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    { clean upgrade: remove a prior RXDK-360 before laying down the new one }
    if IsUpgrade('RXDK-360') then
      UnInstallOldVersion('RXDK-360');
    { install the XDK (unpack + manifest) with a live progress page }
    DoManifestInstall();
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
