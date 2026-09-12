// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// Xbox 360 C/C++ compile task, recompiled from source for modern Visual Studio.
//
// This is the RXDK-360 rebuild of the stock XDK's Microsoft.Xna.Xbox360.Build
// CL task. The original shipped assembly derived its CL/Link tasks from
// Microsoft.Build.CPPTasks.Common, Version=4.0.0.0 (the VS2010-era VC MSBuild
// task base), which modern Visual Studio no longer ships -- so the stock DLL
// fails to bind under VS2022/2026 MSBuild. The task logic itself is unchanged;
// only the base assembly reference is retargeted to the installed VC toolset
// (v170/v180), so the same PowerPC /switch defaults and Xbox-only options load
// natively in current MSBuild. Behaviour is faithful to the original task.

using System;
using System.Collections;
using Microsoft.Build.CPPTasks;
using Microsoft.Build.Utilities;

namespace Rxdk.Xbox360.Build.Tasks
{
    /// <summary>
    /// Xbox 360 flavour of the VC <c>CL</c> task. Hides host-PC-only switches
    /// that the PowerPC compiler rejects (managed compilation, SSE, calling
    /// convention, ...) and adds the Xbox 360 PowerPC-specific options
    /// (/QVMXReserve, /Oc, /Ou, /Oz, pipeline analysis, call-attributed
    /// profiling, PREfast, /Zc:auto).
    /// </summary>
    public class Xbox360CL : Microsoft.Build.CPPTasks.CL
    {
        public override string CompileAsManaged
        {
            get { return base.CompileAsManaged; }
            set { DisableSwitch("CompileAsManaged", value, "false"); }
        }

        public override string EnableEnhancedInstructionSet
        {
            get { return base.EnableEnhancedInstructionSet; }
            set { DisableSwitch("EnableEnhancedInstructionSet", value, "NotSet"); }
        }

        public override bool GenerateXMLDocumentationFiles
        {
            get { return base.GenerateXMLDocumentationFiles; }
            set { DisableSwitch("GenerateXMLDocumentationFiles", value.ToString(), "false"); }
        }

        public override string ErrorReporting
        {
            get { return base.ErrorReporting; }
            set { DisableSwitch("ErrorReporting", value, "None"); }
        }

        public override string CallingConvention
        {
            get { return base.CallingConvention; }
            set { DisableSwitch("CallingConvention", value, "Cdecl"); }
        }

        public override bool CreateHotpatchableImage
        {
            get { return base.CreateHotpatchableImage; }
            set { DisableSwitch("CreateHotpatchableImage", value.ToString(), "false"); }
        }

        public override bool OmitFramePointers
        {
            get { return base.OmitFramePointers; }
            set { DisableSwitch("OmitFramePointers", value.ToString(), "false"); }
        }

        public override bool EnablePREfast
        {
            get
            {
                if (IsPropertySet("EnablePREfast"))
                {
                    return base.ActiveToolSwitches["EnablePREfast"].BooleanValue;
                }
                return false;
            }
            set
            {
                base.ActiveToolSwitches.Remove("EnablePREfast");
                ToolSwitch toolSwitch = new ToolSwitch(ToolSwitchType.Boolean);
                toolSwitch.DisplayName = "Enable Code Analysis";
                toolSwitch.Description = "Enables code analysis functionality that identifies common coding defects in C/C++ code.     (/analyze)";
                toolSwitch.ArgumentRelationList = new ArrayList();
                toolSwitch.SwitchValue = "/analyze";
                toolSwitch.Name = "EnablePREfast";
                toolSwitch.BooleanValue = value;
                base.ActiveToolSwitches.Add("EnablePREfast", toolSwitch);
                AddActiveSwitchToolValue(toolSwitch);
            }
        }

        public override string DebugInformationFormat
        {
            get { return base.DebugInformationFormat; }
            set
            {
                if (DisableSwitch("DebugInformationFormat", value, "OldStyle", "ProgramDatabase"))
                {
                    base.DebugInformationFormat = value;
                }
            }
        }

        public virtual bool RegisterReservation
        {
            get
            {
                if (IsPropertySet("RegisterReservation"))
                {
                    return base.ActiveToolSwitches["RegisterReservation"].BooleanValue;
                }
                return false;
            }
            set
            {
                base.ActiveToolSwitches.Remove("RegisterReservation");
                ToolSwitch toolSwitch = new ToolSwitch(ToolSwitchType.Boolean);
                toolSwitch.DisplayName = "Register Reservation";
                toolSwitch.Description = "Allows a game title to reserve VMX registers from VR64 to VR95 for private use. Note that calls to Xbox 360 title libraries may not honor the reservation of these registers.";
                toolSwitch.ArgumentRelationList = new ArrayList();
                toolSwitch.SwitchValue = "/QVMXReserve";
                toolSwitch.Name = "RegisterReservation";
                toolSwitch.BooleanValue = value;
                base.ActiveToolSwitches.Add("RegisterReservation", toolSwitch);
                AddActiveSwitchToolValue(toolSwitch);
            }
        }

        public virtual bool TrapIntegerDividesOptimization
        {
            get
            {
                if (IsPropertySet("TrapIntegerDividesOptimization"))
                {
                    return base.ActiveToolSwitches["TrapIntegerDividesOptimization"].BooleanValue;
                }
                return false;
            }
            set
            {
                base.ActiveToolSwitches.Remove("TrapIntegerDividesOptimization");
                ToolSwitch toolSwitch = new ToolSwitch(ToolSwitchType.Boolean);
                toolSwitch.DisplayName = "Trap Integer Divides Optimization";
                toolSwitch.Description = "Disables generation of trap instructions around integer divides.     (/Oc)";
                toolSwitch.ArgumentRelationList = new ArrayList();
                toolSwitch.SwitchValue = "/Oc";
                toolSwitch.Name = "TrapIntegerDividesOptimization";
                toolSwitch.BooleanValue = value;
                base.ActiveToolSwitches.Add("TrapIntegerDividesOptimization", toolSwitch);
                AddActiveSwitchToolValue(toolSwitch);
            }
        }

        public virtual bool PreschedulingOptimization
        {
            get
            {
                if (IsPropertySet("PreschedulingOptimization"))
                {
                    return base.ActiveToolSwitches["PreschedulingOptimization"].BooleanValue;
                }
                return false;
            }
            set
            {
                base.ActiveToolSwitches.Remove("PreschedulingOptimization");
                ToolSwitch toolSwitch = new ToolSwitch(ToolSwitchType.Boolean);
                toolSwitch.DisplayName = "Prescheduling Optimization";
                toolSwitch.Description = "Performs an additional code scheduling pass before the register allocation phase.     (/Ou)";
                toolSwitch.ArgumentRelationList = new ArrayList();
                toolSwitch.SwitchValue = "/Ou";
                toolSwitch.Name = "PreschedulingOptimization";
                toolSwitch.BooleanValue = value;
                base.ActiveToolSwitches.Add("PreschedulingOptimization", toolSwitch);
                AddActiveSwitchToolValue(toolSwitch);
            }
        }

        public virtual bool InlineAssemblyOptimization
        {
            get
            {
                if (IsPropertySet("InlineAssemblyOptimization"))
                {
                    return base.ActiveToolSwitches["InlineAssemblyOptimization"].BooleanValue;
                }
                return false;
            }
            set
            {
                base.ActiveToolSwitches.Remove("InlineAssemblyOptimization");
                ToolSwitch toolSwitch = new ToolSwitch(ToolSwitchType.Boolean);
                toolSwitch.DisplayName = "Inline Assembly Optimization";
                toolSwitch.Description = "Allow the compiler to reorder inline assembly instructions, interleaving them into surrounding code to minimize latencies.     (/Oz)";
                toolSwitch.ArgumentRelationList = new ArrayList();
                toolSwitch.SwitchValue = "/Oz";
                toolSwitch.Name = "InlineAssemblyOptimization";
                toolSwitch.BooleanValue = value;
                base.ActiveToolSwitches.Add("InlineAssemblyOptimization", toolSwitch);
                AddActiveSwitchToolValue(toolSwitch);
            }
        }

        public virtual bool AnalyzeStalls
        {
            get
            {
                if (IsPropertySet("AnalyzeStalls"))
                {
                    return base.ActiveToolSwitches["AnalyzeStalls"].BooleanValue;
                }
                return false;
            }
            set
            {
                base.ActiveToolSwitches.Remove("AnalyzeStalls");
                ToolSwitch toolSwitch = new ToolSwitch(ToolSwitchType.Boolean);
                toolSwitch.DisplayName = "Simulate Instruction Pipeline";
                toolSwitch.Description = "Produce a listing with cycle counts from pipeline emulations. This /FAcs option must be included or no .cod will be produced.";
                toolSwitch.ArgumentRelationList = new ArrayList();
                toolSwitch.SwitchValue = "/QXSTALLS";
                toolSwitch.Name = "AnalyzeStalls";
                toolSwitch.BooleanValue = value;
                toolSwitch.ArgumentRelationList = new ArrayList();
                toolSwitch.ArgumentRelationList.Add(new ArgumentRelation("AssemblerOutput", "All", true, " "));
                base.ActiveToolSwitches.Add("AnalyzeStalls", toolSwitch);
                AddActiveSwitchToolValue(toolSwitch);
            }
        }

        public virtual string CallAttributedProfiling
        {
            get
            {
                if (IsPropertySet("CallAttributedProfiling"))
                {
                    return base.ActiveToolSwitches["CallAttributedProfiling"].Value;
                }
                return null;
            }
            set
            {
                base.ActiveToolSwitches.Remove("CallAttributedProfiling");
                ToolSwitch toolSwitch = new ToolSwitch(ToolSwitchType.String);
                toolSwitch.DisplayName = "CallAttributedProfiling";
                toolSwitch.Description = "Generates calls to profiler upon entering and exiting functions.    (/fastcap, /callcap)";
                toolSwitch.ArgumentRelationList = new ArrayList();
                string[][] switchMap = new string[3][]
                {
                    new string[2] { "Disabled", "" },
                    new string[2] { "FastCap", "/fastcap" },
                    new string[2] { "CallCap", "/callcap" }
                };
                toolSwitch.SwitchValue = ReadSwitchMap("CallAttributedProfiling", switchMap, value);
                toolSwitch.Name = "CallAttributedProfiling";
                toolSwitch.Value = value;
                toolSwitch.MultipleValues = true;
                base.ActiveToolSwitches.Add("CallAttributedProfiling", toolSwitch);
                AddActiveSwitchToolValue(toolSwitch);
            }
        }

        public virtual string PREfast
        {
            get
            {
                if (IsPropertySet("PREfast"))
                {
                    return base.ActiveToolSwitches["PREfast"].Value;
                }
                return null;
            }
            set
            {
                base.ActiveToolSwitches.Remove("PREfast");
                ToolSwitch toolSwitch = new ToolSwitch(ToolSwitchType.String);
                toolSwitch.DisplayName = "Code Analysis For C/C++";
                toolSwitch.Description = "Enables code analysis functionality that identifies common coding defects in C/C++ code.     (/analyze, /analyze:only)";
                toolSwitch.ArgumentRelationList = new ArrayList();
                string[][] switchMap = new string[3][]
                {
                    new string[2] { "Disabled", "" },
                    new string[2] { "Analyze", "/analyze" },
                    new string[2] { "AnalyzeOnly", "/analyze:only" }
                };
                toolSwitch.SwitchValue = ReadSwitchMap("PREfast", switchMap, value);
                toolSwitch.Name = "PREfast";
                toolSwitch.Value = value;
                toolSwitch.MultipleValues = true;
                base.ActiveToolSwitches.Add("PREfast", toolSwitch);
                AddActiveSwitchToolValue(toolSwitch);
            }
        }

        public virtual string DeduceVariableType
        {
            get
            {
                if (IsPropertySet("DeduceVariableType"))
                {
                    if (!base.ActiveToolSwitches["DeduceVariableType"].BooleanValue)
                    {
                        return "false";
                    }
                    return "true";
                }
                return null;
            }
            set
            {
                if (value.Equals("true") || value.Equals("false"))
                {
                    base.ActiveToolSwitches.Remove("DeduceVariableType");
                    ToolSwitch toolSwitch = new ToolSwitch(ToolSwitchType.Boolean);
                    toolSwitch.DisplayName = "Deduce Variable Type";
                    toolSwitch.Description = "Specifies how to use the auto keyword to declare variables. The default option /Zc:auto specifies the compiler deduces the type of the declared variable. /Zc:auto- specifies the compiler allocates the variable to the automatic storage class.";
                    toolSwitch.ArgumentRelationList = new ArrayList();
                    toolSwitch.SwitchValue = "/Zc:auto";
                    toolSwitch.ReverseSwitchValue = "/Zc:auto-";
                    toolSwitch.Name = "DeduceVariableType";
                    toolSwitch.BooleanValue = value.Equals("true");
                    base.ActiveToolSwitches.Add("DeduceVariableType", toolSwitch);
                    AddActiveSwitchToolValue(toolSwitch);
                }
            }
        }

        public Xbox360CL()
        {
            SwitchOrderList.AddRange(new string[8]
            {
                "RegisterReservation", "TrapIntegerDividesOptimization",
                "PreschedulingOptimization", "InlineAssemblyOptimization",
                "AnalyzeStalls", "CallAttributedProfiling", "PREfast",
                "DeduceVariableType"
            });
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
