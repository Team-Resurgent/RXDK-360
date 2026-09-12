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
            Text = "RXDK-360 - choose the toolchain";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterScreen;
            MinimizeBox = false;
            MaximizeBox = false;
            ShowInTaskbar = false;
            ClientSize = new Size(468, 232);
            Font = new Font("Segoe UI", 9f);

            var intro = new Label
            {
                Text = "How should this Xbox 360 title be built? Both toolchains are installed " +
                       "side by side; you can change this later in the project's Platform Toolset.",
                Location = new Point(14, 12),
                Size = new Size(440, 40),
            };

            _modern = new RadioButton
            {
                Text = "Modern (clang / LLVM)",
                Checked = true,
                Location = new Point(16, 60),
                Size = new Size(430, 22),
                Font = new Font(Font, FontStyle.Bold),
            };
            var modernDesc = new Label
            {
                Text = "C/C++23 with the modern runtime (picolibc + libc++), packed by XexTool. " +
                       "The productised RXDK-360 toolchain.",
                Location = new Point(36, 82),
                Size = new Size(414, 34),
                ForeColor = SystemColors.GrayText,
            };

            var legacy = new RadioButton
            {
                Text = "Legacy (stock Xbox 360 XDK)",
                Location = new Point(16, 122),
                Size = new Size(430, 22),
                Font = new Font(Font, FontStyle.Bold),
            };
            var legacyDesc = new Label
            {
                Text = "The stock XDK cl.exe / link.exe / imagexex, exactly as the original SDK. " +
                       "Use for existing XDK code and samples.",
                Location = new Point(36, 144),
                Size = new Size(414, 34),
                ForeColor = SystemColors.GrayText,
            };

            var ok = new Button { Text = "OK", DialogResult = DialogResult.OK, Location = new Point(292, 192), Size = new Size(80, 26) };
            var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel, Location = new Point(378, 192), Size = new Size(80, 26) };

            Controls.AddRange(new Control[] { intro, _modern, modernDesc, legacy, legacyDesc, ok, cancel });
            AcceptButton = ok;
            CancelButton = cancel;
        }
    }
}
