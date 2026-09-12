// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// Xbox 360 linker task, recompiled from source for modern Visual Studio.
// See CL.cs for why this is rebuilt rather than the stock XDK assembly reused.

using System;
using System.Collections;
using Microsoft.Build.CPPTasks;
using Microsoft.Build.Utilities;

namespace Rxdk.Xbox360.Build.Tasks
{
    /// <summary>
    /// Xbox 360 flavour of the VC <c>Link</c> task. Pins the target machine to
    /// PowerPC big-endian, disables host-PC-only image options (ASLR, DEP) that
    /// the Xbox loader does not use, and adds the /AutoDEF linker option.
    /// </summary>
    public class Xbox360Link : Microsoft.Build.CPPTasks.Link
    {
        public override string TargetMachine
        {
            get { return base.TargetMachine; }
            set { DisableSwitch("TargetMachine", value.ToString(), "PPCBE"); }
        }

        public override bool RandomizedBaseAddress
        {
            get { return base.RandomizedBaseAddress; }
            set { DisableSwitch("RandomizedBaseAddress", value.ToString(), "false"); }
        }

        public override bool DataExecutionPrevention
        {
            get { return base.DataExecutionPrevention; }
            set { DisableSwitch("DataExecutionPrevention", value.ToString(), "false"); }
        }

        public virtual string AutomaticModuleDefinitionFile
        {
            get
            {
                if (IsPropertySet("AutomaticModuleDefinitionFile"))
                {
                    return base.ActiveToolSwitches["AutomaticModuleDefinitionFile"].Value;
                }
                return null;
            }
            set
            {
                base.ActiveToolSwitches.Remove("AutomaticModuleDefinitionFile");
                ToolSwitch toolSwitch = new ToolSwitch(ToolSwitchType.File);
                toolSwitch.Separator = ":";
                toolSwitch.DisplayName = "Automatic Module Definition File";
                toolSwitch.Description = "The /AutoDEF linker option provides a convenient way to manage creation of DEF files during development.";
                toolSwitch.ArgumentRelationList = new ArrayList();
                toolSwitch.SwitchValue = "/AutoDEF";
                toolSwitch.Name = "AutomaticModuleDefinitionFile";
                toolSwitch.Value = value;
                base.ActiveToolSwitches.Add("AutomaticModuleDefinitionFile", toolSwitch);
                AddActiveSwitchToolValue(toolSwitch);
            }
        }

        public Xbox360Link()
        {
            SwitchOrderList.Add("AutomaticModuleDefinitionFile");
        }

        protected bool DisableSwitch(string propertyName, string value, params string[] supportedValues)
        {
            foreach (string a in supportedValues)
            {
                if (string.Equals(a, value, StringComparison.CurrentCultureIgnoreCase))
                {
                    return true;
                }
            }
            LogPrivate.LogErrorFromResources("ArgumentOutOfRange", propertyName, value);
            return false;
        }
    }
}
