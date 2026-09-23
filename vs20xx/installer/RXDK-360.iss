; SPDX-License-Identifier: GPL-3.0-or-later
; Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
;
; RXDK-360 installer (Inno Setup 6).
;
; Takes the user's own Xbox 360 XDK setup EXE and lays it down RELOCATED to
; C:\Program Files\RXDK-360, side by side with a stock Microsoft Xbox 360 SDK.
; Setup copies the Xbox 360 platform + net472 task DLLs into every VS 2022
; (v170) and VS 2026/18 (v170 + v180) install, then VSIXInstaller does templates
; and the DAP.

#include "Uninstall.iss"
#include "VsIntegration.iss"

#define AppName    "RXDK-360"
#define AppVersion "1.0.0"
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
UninstallDisplayIcon={app}\Icon.ico
WizardImageFile=WizardImage.bmp
WizardSmallImageFile=WizardSmallImage.bmp
MissingRunOnceIdsWarning=no

[Types]
Name: "full";   Description: "Full - stock XDK toolchain + Clang/LLVM toolchain"
Name: "stock";  Description: "Stock XDK toolchain only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
; The relocated XDK and the VS integration always install (the clang toolchain
; also uses the XDK's import libraries). The clang component adds the self-
; contained Clang/LLVM toolchain, XexTool and xdvdfs so RXDK-360 needs no
; external toolchain.
Name: "clang"; Description: "Clang/LLVM toolchain (+ XexTool + xdvdfs, self-contained)"; Types: full
; RXDK360-Samples replaces the XDK's stock Source\Samples with our ported samples
; (committed .sln/.vcxproj that build under the clang toolset) + their runtime
; assets. Large (~1.3 GB installed after the assets are unpacked).
Name: "samples"; Description: "RXDK360-Samples (ported samples + assets, ~1.3 GB installed)"; Types: full

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
Source: "Icon.ico"; DestDir: "{app}"
; VS integration: platform + task DLLs (Setup copies these into each VS), then
; the VSIX (templates + DAP) which VSIXInstaller installs.
Source: "..\Platforms\Xbox 360\*"; DestDir: "{app}\vsintegration\MSBuild\Xbox 360"; Flags: recursesubdirs createallsubdirs; Excludes: "*.dll"
Source: "..\tasks\v170\Rxdk.Xbox360.Build.dll"; DestDir: "{app}\vsintegration\MSBuild\tasks\v170"; Flags: skipifsourcedoesntexist
Source: "..\tasks\v180\Rxdk.Xbox360.Build.dll"; DestDir: "{app}\vsintegration\MSBuild\tasks\v180"; Flags: skipifsourcedoesntexist
Source: "..\extension\Rxdk360.Vsix\obj\Release\msbuild-tasks\v170\Rxdk.Xbox360.Build.dll"; DestDir: "{app}\vsintegration\MSBuild\tasks\v170"; Flags: skipifsourcedoesntexist
Source: "..\extension\Rxdk360.Vsix\obj\Release\msbuild-tasks\v180\Rxdk.Xbox360.Build.dll"; DestDir: "{app}\vsintegration\MSBuild\tasks\v180"; Flags: skipifsourcedoesntexist
Source: "..\extension\Rxdk360.Vsix\obj\Release\msbuild-tasks\clang\Rxdk.Xbox360.Clang.Build.dll"; DestDir: "{app}\vsintegration\MSBuild\tasks"; Flags: skipifsourcedoesntexist
Source: "..\tasks\Rxdk.Xbox360.Clang.Build\bin\Release\net472\Rxdk.Xbox360.Clang.Build.dll"; DestDir: "{app}\vsintegration\MSBuild\tasks"; Flags: skipifsourcedoesntexist
Source: "..\extension\Rxdk360.Vsix\bin\Release\Rxdk360.Vsix.vsix"; DestDir: "{app}\vsintegration"
Source: "..\README.md";   DestDir: "{app}\vsintegration"

; --- Clang/LLVM compiler bundle -----------------------------------------------
; The install mirrors the real XDK tree (no modern\ / legacy\ split). Our clang is
; a self-contained compiler bundle at {app}\bin\clang, laid out like the XDK's own
; TechPreview\Jul12Compiler (its own bin\include\lib): our Clang + ld.lld (only the
; two binaries we drive + clang's resource headers, not the full LLVM bin), XexTool,
; and the C/C++ runtime headers. clang.exe sits at bin\clang\bin so its
; default resource-dir resolution (bin\..\lib\clang) finds bin\clang\lib\clang -- no
; -resource-dir flag needed. Our generated ELF archives (libc.a/libcpp.a) go to the
; product-root {app}\lib\xbox next to the stock console .libs (coexist: .a vs .lib);
; xdvdfs stays in {app}\bin. The XDK stock headers ({app}\include\xbox) and import
; .libs ({app}\lib\xbox\*.lib for genstubs) are placed by the manifest engine and
; patched/translated in place by stageclang -- a single copy, no staged duplicate.
; CI fills build/llvm from Team-Resurgent/llvm-project xbox-windows-x64.zip
; (scripts/fetch-clang.ps1), XexTool from Team-Resurgent/XexTool latest
; (scripts/fetch-xextool.ps1), xdvdfs from Team-Resurgent/XDVDFS-TR.
; coff/*.a are XDK translations (not redistributable); NOT shipped -- the
; unpacker's stageclang step generates them at install from the user's own XDK
; libs (Coff2Elf), like kernel_import.a.
; bin\clang\bin\ - the tools the build drives
Source: "..\..\build\llvm\bin\clang.exe";   DestDir: "{app}\bin\clang\bin"; Components: clang
Source: "..\..\build\llvm\bin\ld.lld.exe";  DestDir: "{app}\bin\clang\bin"; Components: clang
Source: "..\..\build\llvm\bin\llvm-ar.exe"; DestDir: "{app}\bin\clang\bin"; Components: clang
Source: "..\..\build\xextool\XexTool.exe";  DestDir: "{app}\bin\clang\bin"; Components: clang
Source: "..\..\build\tools\xdvdfs.exe";     DestDir: "{app}\bin"; Components: clang
; bin\clang\lib\clang\ - clang's resource dir (found relative to bin\..\lib\clang)
Source: "..\..\build\llvm\lib\clang\*";     DestDir: "{app}\bin\clang\lib\clang"; Flags: recursesubdirs createallsubdirs; Components: clang
; {app}\lib\xbox\ - our runtime archives, alongside the stock console .libs.
; kernel_import.a and libcompat.a are NOT shipped: the unpacker's stageclang
; step builds them at install from the user's own XDK (import ordinals /
; XDK-header C++ helpers), so they always match the installed XDK rather than
; whichever one the release was built on -- and CI, which has no XDK, need not.
Source: "..\..\build\libc\*.a";             DestDir: "{app}\lib\xbox"; Excludes: "kernel_import.a,libcompat.a"; Components: clang
; bin\clang\compat\ - our clang reimplementations of XDK C++ helper classes (source,
; since they #include XDK headers); stageclang compiles them into lib\xbox\libcompat.a.
Source: "..\..\runtime\compat\*";           DestDir: "{app}\bin\clang\compat"; Components: clang
; bin\clang\include\ - the C/C++23 runtime headers (the XDK's own include\xbox
; is a separate single copy at the product root, placed by the manifest engine)
Source: "..\..\runtime\config\*";           DestDir: "{app}\bin\clang\include\config"; Flags: recursesubdirs createallsubdirs; Components: clang
Source: "..\..\vendor\picolibc\libc\include\*";          DestDir: "{app}\bin\clang\include\picolibc"; Flags: recursesubdirs createallsubdirs; Components: clang
Source: "..\..\build\llvm\libcxx\include\*";    DestDir: "{app}\bin\clang\include\libcxx"; Flags: recursesubdirs createallsubdirs; Components: clang
Source: "..\..\build\llvm\libcxxabi\include\*"; DestDir: "{app}\bin\clang\include\libcxxabi"; Flags: recursesubdirs createallsubdirs; Components: clang

; --- RXDK360-Samples ----------------------------------------------------------
; Our ported samples replace the XDK's stock Source\Samples (the manifest engine
; skips those). The binary runtime media ships as split-zip parts under assets\ and
; is materialised in place post-install by the unpacker's unpacksamples step (the
; C# twin of samples\tools\Manage-Assets.ps1). Git metadata is excluded.
Source: "..\..\samples\*"; DestDir: "{app}\Source\Samples"; Flags: recursesubdirs createallsubdirs; Excludes: "\.git,\.git\*,\.gitignore,\.gitattributes,\.gitmodules,\.github\*"; Components: samples

[Registry]
; RXDK-360's own SDK key (read first by the RXDK-360 platform's Toolset.props),
; kept separate from the stock HKLM\...\Xbox\2.0\SDK so the two SDKs coexist.
; IMPORTANT: written to BOTH the 32-bit (HKLM32 = WOW6432Node) and 64-bit views.
; MSBuild's $(Registry:...) intrinsic - how the Toolset.props read these - resolves
; through the 32-bit view, so a 64-bit-only write (plain HKLM in 64-bit setup) is
; invisible to the build and the clang toolchain silently falls back to the stock
; XDK. HKLM32 is the one the toolset actually needs; HKLM64 is for native readers.
Root: HKLM32; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM32; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "Version";     ValueData: "{#AppVersion}"
Root: HKLM64; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "Version";     ValueData: "{#AppVersion}"
; The install mirrors the real XDK, so the XDK tree lives at the product root:
; XdkPath == InstallPath == {app} (headers at {app}\include\xbox, import libs at
; {app}\lib\xbox). No separate legacy\ tree.
Root: HKLM32; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "XdkPath"; ValueData: "{app}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "XdkPath"; ValueData: "{app}"; Flags: uninsdeletevalue
; Clang compiler bundle root: the clang Toolset.props + Platform.targets resolve the
; Clang/LLVM tools and runtime headers from here (see ClangRoot). xdvdfs is {app}\bin.
Root: HKLM32; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "ClangRoot"; ValueData: "{app}\bin\clang"; Components: clang; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\TeamResurgent\RXDK-360"; ValueType: string; ValueName: "ClangRoot"; ValueData: "{app}\bin\clang"; Components: clang; Flags: uninsdeletevalue

[Icons]
; The XDK's own Start-menu shortcuts are created (under the RXDK-360 group) by
; the manifest engine. Here we only add the uninstaller entry.
Name: "{group}\Uninstall RXDK-360"; Filename: "{uninstallexe}"; IconFilename: "{app}\Icon.ico"

[UninstallRun]
; Reverse the manifest install (files, registry, shortcuts, shell ext).
; Unpacker is WinExe (no console PE); runhidden still required so any window is hidden.
Filename: "{app}\tools\RxdkXdkUnpacker.exe"; Parameters: "uninstall ""{app}"""; \
  Flags: runhidden waituntilterminated; RunOnceId: "RxdkXdkUninstall"

; Inno only removes files it copied in [Files]. The XDK tree, stageclang copies,
; and extra files vsinstall copies into VS are not Inno-tracked. Wipe {app}.
[UninstallDelete]
Type: filesandordirs; Name: "{app}"

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
var code: Integer;
begin
  if CurStep = ssInstall then
  begin
    { clean upgrade: remove a prior RXDK-360 before laying down the new one }
    if IsUpgrade('RXDK-360') then
      UnInstallOldVersion('RXDK-360');
    { install the XDK (unpack + manifest) at the product root }
    DoManifestInstall();
  end
  else if CurStep = ssPostInstall then
  begin
    { finish the mirrored tree in place: patch the XDK headers at {app}\include\xbox,
      translate the XDK import libs {app}\lib\xbox\*.lib -> *.a, and build
      kernel_import.a + libcompat.a into {app}\lib\xbox. Args: product root, clang
      bundle root. Both trees are already laid down (manifest + Inno). }
    if WizardIsComponentSelected('clang') then
    begin
      ExtractTemporaryFile('RxdkXdkUnpacker.exe');
      Exec(ExpandConstant('{tmp}\RxdkXdkUnpacker.exe'),
        'stageclang "' + ExpandConstant('{app}') + '" "' + ExpandConstant('{app}\bin\clang') + '"',
        '', SW_HIDE, ewWaitUntilTerminated, code);
    end;
    { materialise the RXDK360-Samples binary assets (split-zip parts) in place }
    if WizardIsComponentSelected('samples') then
    begin
      ExtractTemporaryFile('RxdkXdkUnpacker.exe');
      Exec(ExpandConstant('{tmp}\RxdkXdkUnpacker.exe'),
        'unpacksamples "' + ExpandConstant('{app}\Source\Samples') + '"',
        '', SW_HIDE, ewWaitUntilTerminated, code);
    end;
    { machine-wide RXDK360 env var -> the (relocated) XDK; Uninstall removes it }
    if WizardIsTaskSelected('envvar') then
      RegWriteExpandStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'RXDK360', ExpandConstant('{app}'));
    { VS 2022 (v170) + VS 2026/18 (v170/v180): platform files first, then VSIX. }
    if WizardIsTaskSelected('vs') then
      InstallVsIntegration;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RegDeleteValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'RXDK360');
    UninstallVsIntegration;
  end;
  (* After Inno deletes its own files, remove anything still under the app dir. *)
  if CurUninstallStep = usPostUninstall then
    DelTree(ExpandConstant('{app}'), True, True, True);
end;
