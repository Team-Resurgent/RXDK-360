// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

namespace Rxdk.Xbox360.Pdb.Symbols;

/// <summary>
/// A global-scope data symbol from the PDB's symbol-record stream: a program global (S_GDATA32),
/// a file-static (S_LDATA32), or a public (S_PUB32). <see cref="Section"/>/<see cref="Offset"/> are
/// a 1-based section index and byte offset that resolve to an image RVA via
/// <see cref="Rxdk.Xbox360.Pdb.Dbi.DbiStream.SectionOffsetToRva"/>; <see cref="TypeIndex"/> resolves against
/// the TPI type system for size/shape (0 for publics, which carry no type).
/// </summary>
public sealed record GlobalSymbol(string Name, uint TypeIndex, ushort Section, uint Offset, bool IsPublic);
