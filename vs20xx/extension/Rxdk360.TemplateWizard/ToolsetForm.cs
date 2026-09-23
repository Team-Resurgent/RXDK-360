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
            // Everything stacks in an auto-sizing single-column table and the form
            // grows to fit it, so nothing collides or clips no matter how the text
            // wraps at the user's DPI/font (the old fixed Y positions assumed a
            // 2-line intro and hid the first radio when it wrapped to 3). AutoSize
            // controls each grow to their text; a little bottom padding gives the
            // bold radio descenders (the 'g' in "clang") room so they aren't clipped.
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
            AutoSize = true;
            AutoSizeMode = AutoSizeMode.GrowAndShrink;

            var boldFont = new Font(Font, FontStyle.Bold);
            const int wrapW = 492;              // fixes the dialog width; text wraps within it

            Label Wrap(int w, int leftIndent, string text, Color? color = null) => new Label
            {
                Text = text,
                AutoSize = true,
                MaximumSize = new Size(w, 0),
                Margin = new Padding(leftIndent, 0, 0, 2),
                ForeColor = color ?? SystemColors.ControlText,
            };
            RadioButton Radio(string text, bool chk) => new RadioButton
            {
                Text = text,
                Checked = chk,
                Font = boldFont,
                AutoSize = true,
                Margin = new Padding(0, 10, 0, 3),
            };

            var intro = Wrap(wrapW, 0,
                "How should this Xbox 360 title be built? Both toolchains install side by side, " +
                "so you can change it later in the project's Platform Toolset.");
            _modern = Radio("Modern (clang / LLVM)", true);
            var modernDesc = Wrap(wrapW - 20, 20,
                "C/C++23 with the clang runtime (picolibc + libc++), packed by XexTool. " +
                "The productised RXDK-360 toolchain.", SystemColors.GrayText);
            var legacy = Radio("Legacy (stock Xbox 360 XDK)", false);
            var legacyDesc = Wrap(wrapW - 20, 20,
                "The stock XDK cl.exe / link.exe / imagexex, exactly as the original SDK. " +
                "Use for existing XDK code and samples.", SystemColors.GrayText);
            var ok = new Button
            {
                Text = "OK",
                DialogResult = DialogResult.OK,
                Size = new Size(96, 34),
                Anchor = AnchorStyles.Right,
                Margin = new Padding(0, 14, 0, 0),
            };

            var layout = new TableLayoutPanel
            {
                ColumnCount = 1,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                Dock = DockStyle.Fill,
                Padding = new Padding(16, 14, 16, 14),
            };
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            layout.Controls.AddRange(new Control[] { intro, _modern, modernDesc, legacy, legacyDesc, ok });

            Controls.Add(layout);
            AcceptButton = ok;
        }
    }
}
