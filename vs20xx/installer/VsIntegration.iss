{ SPDX-License-Identifier: GPL-3.0-or-later }
{ Included into the [Code] section. Copies the Xbox 360 platform + task DLLs
  into VS 2022 (v170) and VS 2026/18 (v170 + v180), then runs VSIXInstaller
  against each instance so both IDEs get templates + DAP. }

function CopyDirectory(const Source, Dest: String): Boolean;
var
  FindRec: TFindRec;
begin
  Result := True;
  ForceDirectories(Dest);
  if FindFirst(AddBackslash(Source) + '*', FindRec) then
  try
    repeat
      if (FindRec.Name = '.') or (FindRec.Name = '..') then Continue;
      if FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY <> 0 then
      begin
        if not CopyDirectory(AddBackslash(Source) + FindRec.Name,
             AddBackslash(Dest) + FindRec.Name) then
          Result := False;
      end
      else if not CopyFile(AddBackslash(Source) + FindRec.Name,
           AddBackslash(Dest) + FindRec.Name, False) then
        Result := False;
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

function VsAlreadyListed(const Installs: TArrayOfString; const Root: String): Boolean;
var
  i: Integer;
  a, b: String;
begin
  Result := False;
  a := RemoveBackslash(Root);
  for i := 0 to GetArrayLength(Installs) - 1 do
  begin
    b := RemoveBackslash(Installs[i]);
    if CompareText(a, b) = 0 then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

procedure AddVsInstall(var Installs, Ids: TArrayOfString; const Root, Id: String);
var
  n: Integer;
begin
  if not DirExists(AddBackslash(Root) + 'Common7\IDE') then Exit;
  if VsAlreadyListed(Installs, Root) then Exit;
  n := GetArrayLength(Installs);
  SetArrayLength(Installs, n + 1);
  SetArrayLength(Ids, n + 1);
  Installs[n] := RemoveBackslash(Root);
  Ids[n] := Id;
end;

procedure GetVswhereProperty(const Prop: String; var Lines: TArrayOfString);
var
  tmp, vswhere, args: String;
  rc, i, n: Integer;
  raw: TArrayOfString;
begin
  SetArrayLength(Lines, 0);
  vswhere := ExpandConstant('{pf32}\Microsoft Visual Studio\Installer\vswhere.exe');
  if not FileExists(vswhere) then Exit;
  tmp := ExpandConstant('{tmp}\rxdk_vswhere_' + Prop + '.txt');
  DeleteFile(tmp);
  { VS 2022 is 17.x; VS 2026 is 18.x. Skip 2019 and older. }
  args := '/C ""' + vswhere + '" -all -prerelease -products * -version [17.0,19.0) -property ' + Prop + ' > "' + tmp + '"';
  Exec(ExpandConstant('{cmd}'), args, '', SW_HIDE, ewWaitUntilTerminated, rc);
  if not LoadStringsFromFile(tmp, raw) then Exit;
  n := 0;
  SetArrayLength(Lines, GetArrayLength(raw));
  for i := 0 to GetArrayLength(raw) - 1 do
  begin
    if Trim(raw[i]) <> '' then
    begin
      Lines[n] := Trim(raw[i]);
      n := n + 1;
    end;
  end;
  SetArrayLength(Lines, n);
end;

procedure ScanVsYearFolder(const YearFolder: String; var Installs, Ids: TArrayOfString);
var
  FindRec: TFindRec;
begin
  if not DirExists(YearFolder) then Exit;
  if FindFirst(AddBackslash(YearFolder) + '*', FindRec) then
  try
    repeat
      if (FindRec.Name = '.') or (FindRec.Name = '..') then Continue;
      if FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY <> 0 then
        AddVsInstall(Installs, Ids, AddBackslash(YearFolder) + FindRec.Name, '');
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

procedure GetVsInstalls(var Installs, Ids: TArrayOfString);
var
  paths, instIds: TArrayOfString;
  i, n: Integer;
begin
  SetArrayLength(Installs, 0);
  SetArrayLength(Ids, 0);
  GetVswhereProperty('installationPath', paths);
  GetVswhereProperty('instanceId', instIds);
  n := GetArrayLength(paths);
  if GetArrayLength(instIds) < n then
    n := GetArrayLength(instIds);
  for i := 0 to GetArrayLength(paths) - 1 do
  begin
    if i < n then
      AddVsInstall(Installs, Ids, paths[i], instIds[i])
    else
      AddVsInstall(Installs, Ids, paths[i], '');
  end;
  { Folder scan so a missing vswhere line still picks up both IDEs. }
  ScanVsYearFolder(ExpandConstant('{pf}\Microsoft Visual Studio\2022'), Installs, Ids);
  ScanVsYearFolder(ExpandConstant('{pf}\Microsoft Visual Studio\18'), Installs, Ids);
  ScanVsYearFolder(ExpandConstant('{pf}\Microsoft Visual Studio\2026'), Installs, Ids);
end;

procedure InstallOneToolset(const VsRoot, Ts: String);
var
  vcDir, platRoot, dst, src, dll: String;
begin
  vcDir := VsRoot + '\MSBuild\Microsoft\VC\' + Ts;
  platRoot := vcDir + '\Platforms';
  if not DirExists(platRoot) then Exit;
  dst := platRoot + '\Xbox 360';
  if DirExists(platRoot + '\RXDK-360') then
    DelTree(platRoot + '\RXDK-360', True, True, True);
  if DirExists(dst) then
    DelTree(dst, True, True, True);
  src := ExpandConstant('{app}\vsintegration\MSBuild\Xbox 360');
  { Inno used to pack Platforms\* into this folder, which nested an extra
    Xbox 360\ directory. Copy the inner tree if that layout is still on disk. }
  if DirExists(src + '\Xbox 360') and FileExists(src + '\Xbox 360\Platform.props') then
    src := src + '\Xbox 360';
  CopyDirectory(src, dst);
  { Stock CL/Link/ImageXex/Deploy tasks reference Microsoft.Build.CPPTasks.Common.
    LoadFrom the platform folder cannot resolve that sibling; put our DLL next to
    it in the VC toolset directory (UsingTask uses ..\..\Rxdk.Xbox360.Build.dll). }
  dll := ExpandConstant('{app}\vsintegration\MSBuild\tasks\' + Ts + '\Rxdk.Xbox360.Build.dll');
  if FileExists(dll) then
  begin
    CopyFile(dll, vcDir + '\Rxdk.Xbox360.Build.dll', False);
    CopyFile(dll, dst + '\Rxdk.Xbox360.Build.dll', False);
  end;
  dll := ExpandConstant('{app}\vsintegration\MSBuild\tasks\Rxdk.Xbox360.Clang.Build.dll');
  if FileExists(dll) then
    CopyFile(dll, dst + '\Rxdk.Xbox360.Clang.Build.dll', False);
end;

procedure RemoveOneToolset(const VsRoot, Ts: String);
var
  vcDir, platRoot, dst: String;
begin
  vcDir := VsRoot + '\MSBuild\Microsoft\VC\' + Ts;
  platRoot := vcDir + '\Platforms';
  dst := platRoot + '\Xbox 360';
  if DirExists(dst) then
    DelTree(dst, True, True, True);
  if DirExists(platRoot + '\RXDK-360') then
    DelTree(platRoot + '\RXDK-360', True, True, True);
  if FileExists(vcDir + '\Rxdk.Xbox360.Build.dll') then
    DeleteFile(vcDir + '\Rxdk.Xbox360.Build.dll');
end;

function VsixInstallerArgs(const InstanceId: String): String;
begin
  Result := '/quiet /admin';
  if InstanceId <> '' then
    Result := Result + ' /instanceIds:' + InstanceId;
end;

procedure InstallVsixIntoVs(const VsRoot, InstanceId: String);
var
  inst, vsix, devenv, args: String;
  rc, i: Integer;
begin
  inst := VsRoot + '\Common7\IDE\VSIXInstaller.exe';
  if not FileExists(inst) then Exit;
  vsix := ExpandConstant('{app}\vsintegration\Rxdk360.Vsix.vsix');
  args := VsixInstallerArgs(InstanceId);
  for i := 1 to 15 do
  begin
    if not Exec(inst, args + ' /uninstall:' + RxdkVsixId, '', SW_HIDE, ewWaitUntilTerminated, rc) then Break;
    if rc <> 0 then Break;
  end;
  Exec(inst, args + ' "' + vsix + '"', '', SW_HIDE, ewWaitUntilTerminated, rc);
  devenv := VsRoot + '\Common7\IDE\devenv.exe';
  if FileExists(devenv) then
    Exec(devenv, '/updateconfiguration', '', SW_HIDE, ewWaitUntilTerminated, rc);
end;

procedure UninstallVsixFromVs(const VsRoot, InstanceId: String);
var
  inst, args: String;
  rc, i: Integer;
begin
  inst := VsRoot + '\Common7\IDE\VSIXInstaller.exe';
  if not FileExists(inst) then Exit;
  args := VsixInstallerArgs(InstanceId);
  for i := 1 to 15 do
  begin
    if not Exec(inst, args + ' /uninstall:' + RxdkVsixId, '', SW_HIDE, ewWaitUntilTerminated, rc) then Break;
    if rc <> 0 then Break;
  end;
end;

procedure InstallVsIntegration();
var
  installs, ids: TArrayOfString;
  i: Integer;
begin
  GetVsInstalls(installs, ids);
  if GetArrayLength(installs) = 0 then
  begin
    MsgBox('Visual Studio 2022/2026 was not found. The XDK was installed; the Xbox 360 platform was not.', mbError, MB_OK);
    Exit;
  end;
  for i := 0 to GetArrayLength(installs) - 1 do
  begin
    { VS 2022: v170. VS 2026/18: v170 + v180 when those toolset folders exist. }
    InstallOneToolset(installs[i], 'v170');
    InstallOneToolset(installs[i], 'v180');
    InstallVsixIntoVs(installs[i], ids[i]);
  end;
end;

procedure UninstallVsIntegration();
var
  installs, ids: TArrayOfString;
  i: Integer;
begin
  GetVsInstalls(installs, ids);
  for i := 0 to GetArrayLength(installs) - 1 do
  begin
    RemoveOneToolset(installs[i], 'v170');
    RemoveOneToolset(installs[i], 'v180');
    UninstallVsixFromVs(installs[i], ids[i]);
  end;
end;
