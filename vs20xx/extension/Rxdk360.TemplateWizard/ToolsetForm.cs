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
            // Fixed layout, sized generously so text does not clip. (An auto-sizing
            // layout was tried but did not display reliably inside the VS new-project
            // host, so this stays with explicit, roomy positions.)
            AutoScaleMode = AutoScaleMode.None;
            Font = new Font("Segoe UI", 9f);
            Text = "RXDK-360 - choose the toolchain";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MinimizeBox = false;
            MaximizeBox = false;
            ShowInTaskbar = false;
            ClientSize = new Size(524, 286);

            var boldFont = new Font(Font, FontStyle.Bold);

            var intro = new Label
            {
                Text = "How should this Xbox 360 title be built? Both toolchains install side by " +
                       "side, so you can change it later in the project's Platform Toolset.",
                Location = new Point(16, 14),
                Size = new Size(492, 40),
            };

            _modern = new RadioButton
            {
                Text = "Modern (clang / LLVM)",
                Checked = true,
                Font = boldFont,
                Location = new Point(16, 62),
                Size = new Size(492, 24),
            };
            var modernDesc = new Label
            {
                Text = "C/C++23 with the modern runtime (picolibc + libc++), packed by XexTool. " +
                       "The productised RXDK-360 toolchain.",
                ForeColor = SystemColors.GrayText,
                Location = new Point(36, 88),
                Size = new Size(472, 38),
            };

            var legacy = new RadioButton
            {
                Text = "Legacy (stock Xbox 360 XDK)",
                Font = boldFont,
                Location = new Point(16, 134),
                Size = new Size(492, 24),
            };
            var legacyDesc = new Label
            {
                Text = "The stock XDK cl.exe / link.exe / imagexex, exactly as the original SDK. " +
                       "Use for existing XDK code and samples.",
                ForeColor = SystemColors.GrayText,
                Location = new Point(36, 160),
                Size = new Size(472, 38),
            };

            var ok = new Button { Text = "OK", DialogResult = DialogResult.OK, Location = new Point(346, 244), Size = new Size(80, 28) };
            var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel, Location = new Point(432, 244), Size = new Size(80, 28) };

            Controls.AddRange(new Control[] { intro, _modern, modernDesc, legacy, legacyDesc, ok, cancel });
            AcceptButton = ok;
            CancelButton = cancel;
        }
    }
}
