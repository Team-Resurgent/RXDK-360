// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Drawing;
using System.Windows.Forms;

namespace Rxdk360.TemplateWizard
{
    /// <summary>The modern/legacy toolchain chooser shown when a new title is created.</summary>
    internal sealed class ToolsetForm : Form
    {
        private readonly RadioButton _modern;

        public bool UseModern => _modern.Checked;

        public ToolsetForm()
        {
            // DPI-aware auto-layout: every label auto-sizes and wraps to a fixed
            // content width, so nothing clips at 125/150/200% scaling.
            const int contentWidth = 460;
            AutoScaleMode = AutoScaleMode.Dpi;
            Font = new Font("Segoe UI", 9f);
            Text = "RXDK-360 - choose the toolchain";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MinimizeBox = false;
            MaximizeBox = false;
            ShowInTaskbar = false;
            AutoSize = true;
            AutoSizeMode = AutoSizeMode.GrowAndShrink;

            Label Desc(string text) => new Label
            {
                Text = text,
                AutoSize = true,
                MaximumSize = new Size(contentWidth - 24, 0),
                Margin = new Padding(24, 0, 0, 10),
                ForeColor = SystemColors.GrayText,
            };
            RadioButton Radio(string text, bool chk) => new RadioButton
            {
                Text = text,
                Checked = chk,
                AutoSize = true,
                Margin = new Padding(0, 4, 0, 2),
                Font = new Font(Font, FontStyle.Bold),
            };

            var intro = new Label
            {
                Text = "How should this Xbox 360 title be built? Both toolchains install side " +
                       "by side, so you can change this later in the project's Platform Toolset.",
                AutoSize = true,
                MaximumSize = new Size(contentWidth, 0),
                Margin = new Padding(0, 0, 0, 12),
            };
            _modern = Radio("Modern (clang / LLVM)", true);
            var modernDesc = Desc("C/C++23 with the modern runtime (picolibc + libc++), packed by " +
                                  "XexTool. The productised RXDK-360 toolchain.");
            var legacy = Radio("Legacy (stock Xbox 360 XDK)", false);
            var legacyDesc = Desc("The stock XDK cl.exe / link.exe / imagexex, exactly as the original " +
                                  "SDK. Use for existing XDK code and samples.");

            var ok = new Button { Text = "OK", DialogResult = DialogResult.OK, AutoSize = true, MinimumSize = new Size(84, 28), Margin = new Padding(8, 0, 0, 0) };
            var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel, AutoSize = true, MinimumSize = new Size(84, 28), Margin = new Padding(8, 0, 0, 0) };
            var buttons = new FlowLayoutPanel { FlowDirection = FlowDirection.RightToLeft, AutoSize = true, Dock = DockStyle.Fill, Margin = new Padding(0, 8, 0, 0) };
            buttons.Controls.Add(cancel);
            buttons.Controls.Add(ok);

            var stack = new FlowLayoutPanel
            {
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                Padding = new Padding(16),
            };
            foreach (var c in new Control[] { intro, _modern, modernDesc, legacy, legacyDesc, buttons })
                stack.Controls.Add(c);
            Controls.Add(stack);

            AcceptButton = ok;
            CancelButton = cancel;
        }
    }
}
