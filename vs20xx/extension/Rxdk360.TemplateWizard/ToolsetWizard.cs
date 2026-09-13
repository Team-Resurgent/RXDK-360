// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Generic;
using Microsoft.VisualStudio.TemplateWizard;
using EnvDTE;

namespace Rxdk360.TemplateWizard
{
    /// <summary>
    /// Project-template wizard for the RXDK-360 Application templates. Asks whether
    /// the new title should build with the MODERN (clang/LLVM) or LEGACY (stock XDK)
    /// toolchain and emits the matching project configurations.
    ///
    /// The choice sets &lt;PlatformToolset&gt; ("clang" or "2010-01") and, crucially,
    /// a DIFFERENT set of configurations per toolset - mirroring how an official
    /// VS2010 XDK project ships several configs, each with its own per-config lib
    /// variants:
    ///   * Legacy: the full official set - Debug / CodeAnalysis / Profile /
    ///     Profile_FastCap / Release / Release_LTCG - with the matching debug ('d'),
    ///     profile ('i') and LTCG lib variants, just like the stock XDK.
    ///   * Modern: Debug / Release / Release_LTCG. The clang toolchain cannot link
    ///     the debug/profile lib variants (they pull xbdm/PIX), so every config uses
    ///     the retail libs; Release_LTCG turns on real clang/lld link-time
    ///     optimisation (RxdkModernLto).
    /// The three $rxdk...$ template parameters below are substituted into
    /// Xbox360Game.vcxproj.
    /// </summary>
    public sealed class ToolsetWizard : IWizard
    {
        public void RunStarted(object automationObject,
                               Dictionary<string, string> replacementsDictionary,
                               WizardRunKind runKind, object[] customParams)
        {
            // OK-only chooser (the project is already being created). Default to
            // modern; a wizard failure must never block project creation.
            bool modern = true;
            try
            {
                using (var dlg = new ToolsetForm())
                {
                    dlg.ShowDialog();
                    modern = dlg.UseModern;
                }
            }
            catch { /* keep the default */ }

            replacementsDictionary["$rxdktoolset$"] = modern ? "clang" : "2010-01";
            replacementsDictionary["$rxdkprojectconfigurations$"] = modern ? ModernConfigs   : LegacyConfigs;
            replacementsDictionary["$rxdkconfigurationgroups$"]   = modern ? ModernCfgGroups  : LegacyCfgGroups;
            replacementsDictionary["$rxdkitemdefinitiongroups$"]  = modern ? ModernItemDefs   : LegacyItemDefs;
        }

        public bool ShouldAddProjectItem(string filePath) => true;
        public void BeforeOpeningFile(ProjectItem projectItem) { }
        public void ProjectFinishedGenerating(Project project) { }
        public void ProjectItemFinishedGenerating(ProjectItem projectItem) { }
        public void RunFinished() { }

        // ---- helpers -------------------------------------------------------

        private static string Cfg(string name) =>
            "    <ProjectConfiguration Include=\"" + name + "|Xbox 360\">\r\n" +
            "      <Configuration>" + name + "</Configuration>\r\n" +
            "      <Platform>Xbox 360</Platform>\r\n" +
            "    </ProjectConfiguration>\r\n";

        // A per-config <ItemDefinitionGroup>: optimization/runtime/defines on the
        // compiler, and the config's lib list in the standard Additional
        // Dependencies field (both toolsets read it - the modern task maps each .lib
        // to its translated archive; see ClangLink.ResolveLibraries).
        //
        // The list is set WITHOUT %(AdditionalDependencies): the base
        // Microsoft.Cpp.Xbox 360.Core.props seeds a default (the debug lib set), and
        // inheriting it would mix debug libs into Release and, worse, double-link the
        // retail + variant of a lib (e.g. XAPILIB.lib + xapilibi.lib -> LNK2005 on the
        // shared PIX objects). An official title overrides the list per config the
        // same way, so each config links exactly its own variant set.
        private static string ItemDef(string cond, string opt, string runtime,
                                      string defines, string libs, string extraCl = "",
                                      string extraLink = "")
        {
            string cl =
                "      <Optimization>" + opt + "</Optimization>\r\n" +
                (runtime != null ? "      <RuntimeLibrary>" + runtime + "</RuntimeLibrary>\r\n" : "") +
                "      <PreprocessorDefinitions>" + defines + "%(PreprocessorDefinitions)</PreprocessorDefinitions>\r\n" +
                extraCl;
            return
                "  <ItemDefinitionGroup Condition=\"'$(Configuration)'=='" + cond + "'\">\r\n" +
                "    <ClCompile>\r\n" + cl +
                "    </ClCompile>\r\n" +
                "    <Link>\r\n" +
                "      <AdditionalDependencies>" + libs + "</AdditionalDependencies>\r\n" +
                extraLink +
                "    </Link>\r\n" +
                "  </ItemDefinitionGroup>\r\n";
        }

        // ---- MODERN (clang) : Debug / Release / Release_LTCG, retail libs -------
        // _DEBUG is intentionally not defined even in Debug: it selects the stock
        // debug D3D runtime (d3d9d + xapilibd/xbdm/PIX) which the modern toolchain
        // cannot link, so Debug pins the retail runtime and just keeps -O0 + DWARF.
        private const string RetailLibs = "xboxkrnl.lib;xapilib.lib;d3d9.lib;d3dx9.lib;xgraphics.lib";

        private static readonly string ModernConfigs = Cfg("Debug") + Cfg("Release") + Cfg("Release_LTCG");

        private static readonly string ModernCfgGroups =
            "  <PropertyGroup Label=\"Configuration\">\r\n" +
            "    <ConfigurationType>Application</ConfigurationType>\r\n" +
            "    <PlatformToolset>clang</PlatformToolset>\r\n" +
            "    <CharacterSet>Unicode</CharacterSet>\r\n" +
            "    <RxdkModernXdkHeaders>true</RxdkModernXdkHeaders>\r\n" +
            "  </PropertyGroup>\r\n" +
            "  <PropertyGroup Condition=\"'$(Configuration)'=='Release_LTCG'\" Label=\"Configuration\">\r\n" +
            "    <!-- Real link-time optimisation: clang -flto on every TU + lld LTO. -->\r\n" +
            "    <RxdkModernLto>true</RxdkModernLto>\r\n" +
            "  </PropertyGroup>\r\n";

        private static readonly string ModernItemDefs =
            ItemDef("Debug",        "Disabled", "MultiThreaded", "",       RetailLibs) +
            ItemDef("Release",      "MaxSpeed", null,            "NDEBUG;", RetailLibs) +
            ItemDef("Release_LTCG", "MaxSpeed", null,            "NDEBUG;", RetailLibs);

        // ---- LEGACY (stock XDK) : the full official six with per-config libs ----
        private static readonly string LegacyConfigs =
            Cfg("CodeAnalysis") + Cfg("Debug") + Cfg("Profile") +
            Cfg("Profile_FastCap") + Cfg("Release") + Cfg("Release_LTCG");

        private static readonly string LegacyCfgGroups =
            "  <PropertyGroup Label=\"Configuration\">\r\n" +
            "    <ConfigurationType>Application</ConfigurationType>\r\n" +
            "    <PlatformToolset>2010-01</PlatformToolset>\r\n" +
            "    <CharacterSet>Unicode</CharacterSet>\r\n" +
            "  </PropertyGroup>\r\n" +
            "  <PropertyGroup Condition=\"'$(Configuration)'=='Release_LTCG'\" Label=\"Configuration\">\r\n" +
            "    <WholeProgramOptimization>true</WholeProgramOptimization>\r\n" +
            "  </PropertyGroup>\r\n";

        // Official per-config lib variants (scoped to this template's libraries):
        // Debug/CodeAnalysis -> 'd' debug libs; Profile -> 'i' instrumented libs;
        // Release/FastCap -> retail; Release_LTCG -> LTCG libs. xbdm.lib is present
        // in every non-shipping config, as in the stock XDK.
        private const string DbgLibs  = "xapilibd.lib;d3d9d.lib;d3dx9d.lib;xgraphicsd.lib;xboxkrnl.lib;vcompd.lib;xbdm.lib";
        private const string ProfLibs = "xapilibi.lib;d3d9i.lib;d3dx9.lib;xgraphics.lib;xboxkrnl.lib;vcomp.lib;xbdm.lib";
        private const string FcapLibs = "xapilib.lib;d3d9.lib;d3dx9.lib;xgraphics.lib;xboxkrnl.lib;vcomp.lib;xbdm.lib";
        private const string RelLibs  = "xapilib.lib;d3d9.lib;d3dx9.lib;xgraphics.lib;xboxkrnl.lib;vcomp.lib";
        private const string LtcgLibs = "xapilib.lib;d3d9ltcg.lib;d3dx9.lib;xgraphics.lib;xboxkrnl.lib;vcomp.lib";

        private static readonly string LegacyItemDefs =
            ItemDef("CodeAnalysis",    "Disabled", "MultiThreadedDebug", "_DEBUG;",        DbgLibs,
                    "      <EnablePREfast>true</EnablePREfast>\r\n") +
            ItemDef("Debug",           "Disabled", "MultiThreadedDebug", "_DEBUG;",        DbgLibs) +
            ItemDef("Profile",         "MaxSpeed", "MultiThreaded",      "NDEBUG;PROFILE;", ProfLibs,
                    "", "      <IgnoreSpecificDefaultLibraries>xapilib.lib;d3d9.lib</IgnoreSpecificDefaultLibraries>\r\n") +
            ItemDef("Profile_FastCap", "MaxSpeed", "MultiThreaded",      "NDEBUG;PROFILE;FASTCAP;", FcapLibs) +
            ItemDef("Release",         "MaxSpeed", "MultiThreaded",      "NDEBUG;",        RelLibs) +
            ItemDef("Release_LTCG",    "MaxSpeed", "MultiThreaded",      "NDEBUG;",        LtcgLibs,
                    "      <WholeProgramOptimization>true</WholeProgramOptimization>\r\n");
    }
}
