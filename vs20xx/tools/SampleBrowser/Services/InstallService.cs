using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using RXDK360.SampleBrowser.Models;

namespace RXDK360.SampleBrowser.Services;

public sealed class InstallResult
{
    public required bool Success { get; init; }
    public string? SolutionPath { get; init; }
    public string? Message { get; init; }
}

/// <summary>
/// Reproduces the stock "Install Project" flow without any registry shim:
/// copy the sample + Common into a chosen folder, rename the project, fix the
/// now-shallower Common relative paths, then open the .sln in the chosen VS.
///
/// Installed layout (clones the SDK tree so stock ..\..\ references resolve as-is):
///   &lt;dest&gt;\Common\                (shared ATG framework, writable copy)
///   &lt;dest&gt;\Media                  (junction to the SDK's shared media tree)
///   &lt;dest&gt;\&lt;Area&gt;\&lt;Name&gt;\  &lt;Name&gt;.sln, &lt;Name&gt;.vcxproj, sources, built Media\
/// The sample keeps its two-deep position, so both the project references and the
/// content files' own internal refs (Resource.rdf -&gt; ..\..\Media\Textures\...)
/// resolve natively - no path rewriting.
/// </summary>
public sealed class InstallService
{
    private static readonly string[] SkipDirs =
        { "Release", "Debug", "Profile", "Profile_FastCap", "Release_LTCG", "CodeAnalysis", ".vs", "bin", "obj" };

    private readonly SdkPaths _sdk;
    public InstallService(SdkPaths sdk) => _sdk = sdk;

    public async Task<InstallResult> InstallAsync(Sample sample, string destRoot, string newName, VsInstall? vs)
    {
        try
        {
            return await Task.Run(() => Install(sample, destRoot, newName, vs));
        }
        catch (Exception ex)
        {
            return new InstallResult { Success = false, Message = ex.Message };
        }
    }

    private InstallResult Install(Sample sample, string destRoot, string newName, VsInstall? vs)
    {
        newName = SanitizeName(newName);
        if (string.IsNullOrWhiteSpace(newName))
            return new InstallResult { Success = false, Message = "Invalid project name." };

        var origName = Path.GetFileName(sample.FolderPath);
        // Preserve the sample's position in the cloned tree (e.g. Graphics\<Name>) so the
        // upgraded stock projects AND the content files' internal cross-references
        // (Resource.rdf -> ..\..\Media\Textures\..., scene copies, etc.) resolve natively
        // against the shared Common\ and Media\ at the install root - no path rewriting,
        // which cannot reach references buried inside .rdf/.xatg assets.
        var relParent = Path.GetDirectoryName(sample.RelativeFolder) ?? "";
        var destSample = Path.Combine(destRoot, relParent, newName);
        var destCommon = Path.Combine(destRoot, "Common");

        Directory.CreateDirectory(destRoot);
        CopyTree(sample.FolderPath, destSample);

        // Shared Common (writable copy - it builds here). Refresh when the SDK's copy is
        // newer so an install never links against a stale framework; best-effort so a
        // lock (VS building another sample's Common) doesn't fail the install.
        var commonSrc = Path.Combine(_sdk.SamplesRoot, "Common");
        if (Directory.Exists(commonSrc) && CommonNeedsRefresh(commonSrc, destCommon))
            try { CopyTree(commonSrc, destCommon); } catch { /* keep existing on lock */ }

        // Shared Media: the upgraded stock projects reference their build-time media
        // sources (shaders/effects/resources/scenes) via ..\..\Media\ (rewritten to
        // ..\Media below). Provide it once at the install root as a directory junction
        // to the SDK's Media tree - read-only source, no per-install duplication. The
        // sample still writes its OWN build outputs to <Name>\Media (deployed).
        EnsureSharedMedia(destRoot);

        CopyMedia(sample, destSample);

        // Rename project files (.sln / .vcxproj / .vcxproj.filters) and fix contents.
        var newSln = RenameAndRewrite(destSample, origName, newName);

        // Launch.
        if (newSln is not null)
            OpenInVs(newSln, vs);

        return new InstallResult { Success = true, SolutionPath = newSln, Message = destSample };
    }

    // Refresh Common when it's missing or the SDK's project is newer than the installed
    // one (an SDK/source update). Compares the .vcxproj mtimes as a cheap proxy.
    private static bool CommonNeedsRefresh(string commonSrc, string destCommon)
    {
        var destProj = Path.Combine(destCommon, "Common.vcxproj");
        if (!File.Exists(destProj)) return true;
        var srcProj = Path.Combine(commonSrc, "Common.vcxproj");
        if (!File.Exists(srcProj)) return false;
        try { return File.GetLastWriteTimeUtc(srcProj) > File.GetLastWriteTimeUtc(destProj); }
        catch { return false; }
    }

    // ---- copy helpers ------------------------------------------------------

    private static void CopyTree(string src, string dst)
    {
        Directory.CreateDirectory(dst);
        foreach (var dir in Directory.EnumerateDirectories(src, "*", SearchOption.AllDirectories))
        {
            if (IsSkipped(src, dir)) continue;
            Directory.CreateDirectory(dir.Replace(src, dst));
        }
        foreach (var file in Directory.EnumerateFiles(src, "*", SearchOption.AllDirectories))
        {
            if (IsSkipped(src, Path.GetDirectoryName(file)!)) continue;
            var ext = Path.GetExtension(file);
            if (ext.Equals(".tlog", StringComparison.OrdinalIgnoreCase)) continue;
            if (Path.GetFileName(file).EndsWith(".command.1.tlog", StringComparison.OrdinalIgnoreCase)) continue;
            var target = file.Replace(src, dst);
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(file, target, overwrite: true);
        }
    }

    private static bool IsSkipped(string root, string dir)
    {
        var rel = Path.GetRelativePath(root, dir);
        return rel.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
                  .Any(seg => SkipDirs.Contains(seg, StringComparer.OrdinalIgnoreCase));
    }

    // Provide <destRoot>\Media as a junction to the SDK's shared media tree, so the
    // upgraded stock projects' ..\..\Media\ references (and the content files' internal
    // refs) resolve without copying the (large) asset tree into every install root.
    // Falls back to a copy if the junction fails.
    private void EnsureSharedMedia(string destRoot)
    {
        var link = Path.Combine(destRoot, "Media");
        if (Directory.Exists(link) || File.Exists(link)) return;
        var target = Path.Combine(_sdk.SamplesRoot, "Media");
        if (!Directory.Exists(target)) return;
        try
        {
            var psi = new ProcessStartInfo("cmd.exe", $"/c mklink /J \"{link}\" \"{target}\"")
            {
                UseShellExecute = false, CreateNoWindow = true,
                RedirectStandardOutput = true, RedirectStandardError = true,
            };
            Process.Start(psi)?.WaitForExit();
        }
        catch { /* fall through to copy */ }
        if (!Directory.Exists(link))
        {
            try { CopyTree(target, link); } catch { /* best effort */ }
        }
    }

    private void CopyMedia(Sample sample, string destSample)
    {
        var mediaRoot = Path.Combine(_sdk.SamplesRoot, "Media");
        if (!Directory.Exists(mediaRoot) || sample.InstallMedia.Count == 0)
            return;

        foreach (var item in sample.InstallMedia)
        {
            var rel = item.Replace('/', '\\').Trim('\\');
            var src = Path.Combine(mediaRoot, rel);
            var dst = Path.Combine(destSample, "Media", rel);
            try
            {
                if (Directory.Exists(src)) CopyTree(src, dst);
                else if (File.Exists(src))
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(dst)!);
                    File.Copy(src, dst, overwrite: true);
                }
            }
            catch { /* media is best-effort; not required to build */ }
        }
    }

    // ---- rename + path fix -------------------------------------------------

    private static string? RenameAndRewrite(string destSample, string origName, string newName)
    {
        string? newSln = null;

        foreach (var file in Directory.EnumerateFiles(destSample))
        {
            var name = Path.GetFileName(file);
            var isSln = name.Equals($"{origName}.sln", StringComparison.OrdinalIgnoreCase);
            var isProj = name.Equals($"{origName}.vcxproj", StringComparison.OrdinalIgnoreCase);
            var isFilters = name.Equals($"{origName}.vcxproj.filters", StringComparison.OrdinalIgnoreCase);
            if (!isSln && !isProj && !isFilters) continue;

            var text = File.ReadAllText(file);
            // No path shallowing: the sample keeps its tree depth, so ..\..\Common and
            // ..\..\Media resolve natively (see Install()).
            if (isSln)  text = RewriteSln(text, origName, newName);
            if (isProj) text = RewriteProj(text, origName, newName);

            var renamed = Path.Combine(destSample, name.Replace(origName, newName));
            File.WriteAllText(renamed, text);
            if (!string.Equals(renamed, file, StringComparison.OrdinalIgnoreCase))
                File.Delete(file);

            if (isSln) newSln = renamed;
        }
        return newSln;
    }

    private static string RewriteSln(string text, string orig, string @new) =>
        text.Replace($"\"{orig}\", \"{orig}.vcxproj\"", $"\"{@new}\", \"{@new}.vcxproj\"");

    private static string RewriteProj(string text, string orig, string @new) =>
        text.Replace($"<RootNamespace>{orig}</RootNamespace>", $"<RootNamespace>{@new}</RootNamespace>");

    private static string SanitizeName(string name)
    {
        var invalid = Path.GetInvalidFileNameChars();
        var clean = new string(name.Trim().Where(c => !invalid.Contains(c)).ToArray());
        return clean;
    }

    // ---- launch ------------------------------------------------------------

    private static void OpenInVs(string slnPath, VsInstall? vs)
    {
        try
        {
            if (vs is not null && !vs.IsShellDefault && vs.DevEnvPath is not null)
            {
                Process.Start(new ProcessStartInfo(vs.DevEnvPath, $"\"{slnPath}\"")
                {
                    UseShellExecute = false,
                });
            }
            else
            {
                Process.Start(new ProcessStartInfo(slnPath) { UseShellExecute = true });
            }
        }
        catch { /* leave the folder prepared even if launch fails */ }
    }
}
