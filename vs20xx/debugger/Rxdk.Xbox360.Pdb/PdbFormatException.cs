// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

namespace Rxdk.Xbox360.Pdb;

/// <summary>Thrown when a PDB/MSF structure is malformed or unsupported.</summary>
public sealed class PdbFormatException : Exception
{
    public PdbFormatException(string message) : base(message) { }

    public PdbFormatException(string message, Exception inner) : base(message, inner) { }
}
