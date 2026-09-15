// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Rxdk360.Package.Commands;
using Task = System.Threading.Tasks.Task;

namespace Rxdk360.Package
{
    /// <summary>
    /// Loads with any solution so F5 on an Xbox 360 + Copy to Hard Drive project
    /// is intercepted before the dead Xbox360Debugger flavor. Xenia and Emulate DVD
    /// are not handled here.
    /// </summary>
    [PackageRegistration(UseManagedResourcesOnly = true, AllowsBackgroundLoading = true)]
    [InstalledProductRegistration("RXDK-360", "Xbox 360 hardware debug (Copy to Hard Drive).", "1.0.0")]
    [ProvideAutoLoad(VSConstants.UICONTEXT.SolutionExistsAndFullyLoaded_string, PackageAutoLoadFlags.BackgroundLoad)]
    [ProvideBindingPath]
    [Guid(Rxdk360Guids.PackageString)]
    public sealed class Rxdk360Package : AsyncPackage
    {
        protected override async Task InitializeAsync(CancellationToken cancellationToken, IProgress<ServiceProgressData> progress)
        {
            await base.InitializeAsync(cancellationToken, progress);
            await JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);
            await StartDebugInterceptor.RegisterAsync(this);
        }
    }
}
