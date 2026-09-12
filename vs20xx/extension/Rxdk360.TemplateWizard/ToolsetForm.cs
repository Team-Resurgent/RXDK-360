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
            // Auto-sizing controls (each grows to fit its text) so nothing clips -
            // not the bold radio descenders, not the wrapped descriptions - at any
            // display scaling. AutoScaleDimensions + Font mode scales the fixed
            // positions with the DPI.
            AutoScaleDimensions = new SizeF(7f, 15f);
            AutoScaleMode = AutoScaleMode.Font;
            Font = new Font("Segoe UI", 9f);
            Text = "RXDK-360 - choose the toolchain";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MinimizeBox = false;
            MaximizeBox = false;
            ShowInTaskbar = false;
            ControlBox = false;                 // OK-only: the project is already being created
            ClientSize = new Size(524, 276);

            var boldFont = new Font(Font, FontStyle.Bold);

            // AutoSize measures text height as Font.Height, which can fall a pixel or
            // two short of what bold glyph descenders (the 'g' in "clang") actually
            // need, clipping them at the control's bottom edge. A couple pixels of
            // bottom padding grows the control just enough to give descenders room.
            Label Wrap(int x, int y, int w, string text, Color? color = null) => new Label
            {
                Text = text,
                AutoSize = true,
                MaximumSize = new Size(w, 0),
                Padding = new Padding(0, 0, 0, 2),
                Location = new Point(x, y),
                ForeColor = color ?? SystemColors.ControlText,
            };
            RadioButton Radio(int y, string text, bool chk) => new RadioButton
            {
                Text = text,
                Checked = chk,
                Font = boldFont,
                AutoSize = true,
                Padding = new Padding(0, 0, 0, 3),
                Location = new Point(16, y),
            };

            var intro = Wrap(16, 14, 492,
                "How should this Xbox 360 title be built? Both toolchains install side by side, " +
                "so you can change it later in the project's Platform Toolset.");
            _modern = Radio(64, "Modern (clang / LLVM)", true);
            var modernDesc = Wrap(36, 92, 476,
                "C/C++23 with the modern runtime (picolibc + libc++), packed by XexTool. " +
                "The productised RXDK-360 toolchain.", SystemColors.GrayText);
            var legacy = Radio(146, "Legacy (stock Xbox 360 XDK)", false);
            var legacyDesc = Wrap(36, 174, 476,
                "The stock XDK cl.exe / link.exe / imagexex, exactly as the original SDK. " +
                "Use for existing XDK code and samples.", SystemColors.GrayText);

            var ok = new Button { Text = "OK", DialogResult = DialogResult.OK, Location = new Point(412, 228), Size = new Size(96, 34) };

            Controls.AddRange(new Control[] { intro, _modern, modernDesc, legacy, legacyDesc, ok });
            AcceptButton = ok;
        }
    }
}
