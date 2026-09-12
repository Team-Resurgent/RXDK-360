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
    /// toolchain and substitutes the $rxdktoolset$ template parameter accordingly
    /// ("clang" or "2010-01"). Both toolsets are installed side by side, so the
    /// choice only sets &lt;PlatformToolset&gt; in the generated .vcxproj.
    /// </summary>
    public sealed class ToolsetWizard : IWizard
    {
        public void RunStarted(object automationObject,
                               Dictionary<string, string> replacementsDictionary,
                               WizardRunKind runKind, object[] customParams)
        {
            // Default to modern; a wizard failure must never block project creation.
            string toolset = "clang";
            try
            {
                using (var dlg = new ToolsetForm())
                {
                    if (dlg.ShowDialog() == System.Windows.Forms.DialogResult.OK)
                        toolset = dlg.UseModern ? "clang" : "2010-01";
                }
            }
            catch { /* keep the default */ }

            replacementsDictionary["$rxdktoolset$"] = toolset;
        }

        public bool ShouldAddProjectItem(string filePath) => true;
        public void BeforeOpeningFile(ProjectItem projectItem) { }
        public void ProjectFinishedGenerating(Project project) { }
        public void ProjectItemFinishedGenerating(ProjectItem projectItem) { }
        public void RunFinished() { }
    }
}
