using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
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
/// Self-contained installed layout (matches the stock XDK Sample Browser):
///   &lt;dest&gt;\&lt;Name&gt;\
///       &lt;Name&gt;.sln, &lt;Name&gt;.vcxproj, sources, Resource.rdf
///       Common\    (writable copy of the ATG framework, builds here)
///       Media\     (just the media this sample references, mirrored from the SDK)
/// The stock ..\..\Common\ / ..\..\Media\ references are rewritten to Common\ / Media\
/// in the project AND the content files (Resource.rdf). Build outputs land in
/// $(OutDir)\Media (deployed from there), kept distinct from this Media\ source.
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
        // Self-contained install (matches the stock XDK Sample Browser): everything the
        // sample needs lives under its own folder - Common\ and the referenced Media\
        // subset as subfolders - and the stock ..\..\Common\ / ..\..\Media\ references are
        // rewritten to Common\ / Media\ (in the project AND in the content files, e.g.
        // Resource.rdf's ..\..\Media\Textures\...). Portable, no shared root state. Build
        // outputs go to $(OutDir)\Media (deployed from there), distinct from this source.
        var destSample = Path.Combine(destRoot, newName);
        var destCommon = Path.Combine(destSample, "Common");

        Directory.CreateDirectory(destRoot);
        CopyTree(sample.FolderPath, destSample);

        // Common as a writable subfolder (it builds here). Refresh when the SDK's copy is
        // newer so an install never links a stale framework; best-effort against a lock.
        var commonSrc = Path.Combine(_sdk.SamplesRoot, "Common");
        if (Directory.Exists(commonSrc) && CommonNeedsRefresh(commonSrc, destCommon))
            try { CopyTree(commonSrc, destCommon); } catch { /* keep existing on lock */ }

        // Copy just the media this sample references (project inputs + what its .rdf files
        // bundle) into <sample>\Media, mirroring the SDK's Media\ subtree.
        CopyReferencedMedia(destSample);

        // Rename project files, then rewrite the shared paths to the self-contained layout
        // in the project files AND the content files (.rdf) that carry their own refs.
        var newSln = RenameAndRewrite(destSample, origName, newName);
        RewriteContentPaths(destSample);

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

    // Copy the media this sample actually uses into <sample>\Media, mirroring the SDK's
    // Media\ subtree: (1) the project's direct ..\..\Media\ inputs (shaders/effects/
    // resources/scenes), then (2) whatever the .rdf files reference (textures, font data),
    // resolved relative to each .rdf's own location.
    private void CopyReferencedMedia(string destSample)
    {
        var mediaSrc = Path.Combine(_sdk.SamplesRoot, "Media");
        if (!Directory.Exists(mediaSrc)) return;

        // (1) direct ..\..\Media\<rel> references in the project files.
        var wanted = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var proj in Directory.EnumerateFiles(destSample, "*.vcxproj"))
            foreach (Match m in Regex.Matches(File.ReadAllText(proj),
                     @"\.\.[\\/]+\.\.[\\/]+[Mm]edia[\\/]([^""<>;]+?\.[A-Za-z0-9]+)"))
                wanted.Add(NormRel(m.Groups[1].Value));
        CopyWanted(wanted, mediaSrc, destSample);

        // (2) follow .rdf references (now that the referenced .rdf are present), resolving
        // each token relative to Media\ (embedded ..\..\Media\ path) or the .rdf's own dir.
        var more = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var mediaDst = Path.Combine(destSample, "Media");
        foreach (var rdf in Directory.EnumerateFiles(destSample, "*.rdf", SearchOption.AllDirectories))
        {
            string baseRel = "";
            if (rdf.StartsWith(mediaDst + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                baseRel = Path.GetDirectoryName(Path.GetRelativePath(mediaDst, rdf)) ?? "";
            foreach (Match m in Regex.Matches(File.ReadAllText(rdf),
                     @"[^""'<>\s]+?\.(?:tga|dds|abc|bin|wav|bmp|jpg|png)", RegexOptions.IgnoreCase))
            {
                var raw = m.Value.Replace('/', '\\');
                var idx = raw.IndexOf(@"Media\", StringComparison.OrdinalIgnoreCase);
                var rel = idx >= 0 ? raw.Substring(idx + 6)
                                   : (baseRel.Length > 0 ? baseRel + "\\" + raw : raw);
                more.Add(NormRel(rel));
            }
        }
        CopyWanted(more, mediaSrc, destSample);
    }

    private static void CopyWanted(HashSet<string> rels, string mediaSrc, string destSample)
    {
        foreach (var rel in rels)
        {
            if (rel.Length == 0) continue;
            var src = Path.Combine(mediaSrc, rel);
            if (!File.Exists(src)) continue;
            var dst = Path.Combine(destSample, "Media", rel);
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(dst)!);
                File.Copy(src, dst, overwrite: true);
            }
            catch { /* best effort */ }
        }
    }

    // Normalize a Media-relative path: backslashes, no leading separators, drop any stray
    // leading ..\ a token may carry.
    private static string NormRel(string rel)
    {
        rel = rel.Replace('/', '\\').Trim().TrimStart('\\');
        while (rel.StartsWith(@"..\", StringComparison.Ordinal)) rel = rel.Substring(3);
        return rel;
    }

    // Rewrite the content files' own shared references (Resource.rdf's
    // ..\..\Media\Textures\... etc.) to the self-contained Media\ layout.
    private static void RewriteContentPaths(string destSample)
    {
        foreach (var rdf in Directory.EnumerateFiles(destSample, "*.rdf", SearchOption.AllDirectories))
        {
            try
            {
                var text = File.ReadAllText(rdf);
                var rewritten = ShallowSharedPaths(text);
                if (rewritten != text) File.WriteAllText(rdf, rewritten);
            }
            catch { /* best effort */ }
        }
    }

    // ..\..\Common -> Common and ..\..\Media -> Media (self-contained subfolders); the
    // stock projects use both "Media" and "media".
    private static string ShallowSharedPaths(string text) =>
        text.Replace(@"..\..\Common", "Common")
            .Replace(@"..\..\Media", "Media")
            .Replace(@"..\..\media", "Media");

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
            text = ShallowSharedPaths(text);   // ..\..\Common -> Common, ..\..\Media -> Media
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
