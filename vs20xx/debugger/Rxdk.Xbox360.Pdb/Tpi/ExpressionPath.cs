// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System.Globalization;

namespace Rxdk.Xbox360.Pdb.Tpi;

/// <summary>How one step of an expression path reaches its next value.</summary>
public enum AccessorKind
{
    /// <summary><c>.name</c> — a member of the current aggregate.</summary>
    Member,

    /// <summary><c>-&gt;name</c> — dereference the current pointer, then a member of the referent.</summary>
    Arrow,

    /// <summary><c>[n]</c> — the n-th element of the current array or pointer.</summary>
    Index,
}

/// <summary>A single step of an expression path: a member name or an array index.</summary>
public readonly record struct Accessor(AccessorKind Kind, string Name, long Index);

/// <summary>
/// Parses a debugger watch/hover expression into a base symbol name plus an ordered chain of
/// accessors (<c>a.b-&gt;c[2]</c> → base <c>a</c>, then <c>.b</c>, <c>-&gt;c</c>, <c>[2]</c>).
/// This is the toolchain-agnostic front end for managed expression evaluation; the resolved chain is
/// walked over the TPI type system by <see cref="TypeEvaluator"/>.
/// </summary>
public static class ExpressionPath
{
    /// <summary>
    /// Parses <paramref name="expression"/>. Returns false (with empty outputs) for an empty string or
    /// any malformed syntax, so a caller can reject it rather than mis-evaluate. A bare identifier
    /// parses to that name with an empty accessor list.
    /// </summary>
    public static bool TryParse(string expression, out string baseName, out IReadOnlyList<Accessor> accessors)
    {
        baseName = string.Empty;
        var list = new List<Accessor>();
        accessors = list;
        if (string.IsNullOrWhiteSpace(expression))
            return false;

        var s = expression.Trim();
        var i = 0;

        var start = i;
        while (i < s.Length && IsIdentChar(s[i]))
            i++;
        if (i == start)
            return false;
        baseName = s[start..i];

        while (i < s.Length)
        {
            i = SkipWhitespace(s, i);
            if (i >= s.Length)
                break;

            if (s[i] == '.')
            {
                i++;
                if (!ReadName(s, ref i, out var name))
                    return false;
                list.Add(new Accessor(AccessorKind.Member, name, 0));
            }
            else if (s[i] == '-' && i + 1 < s.Length && s[i + 1] == '>')
            {
                i += 2;
                if (!ReadName(s, ref i, out var name))
                    return false;
                list.Add(new Accessor(AccessorKind.Arrow, name, 0));
            }
            else if (s[i] == '[')
            {
                i++;
                var close = s.IndexOf(']', i);
                if (close < 0)
                    return false;
                var inner = s[i..close].Trim();
                i = close + 1;
                if (!TryParseIndex(inner, out var index))
                    return false;
                list.Add(new Accessor(AccessorKind.Index, string.Empty, index));
            }
            else
            {
                return false;
            }
        }

        return true;
    }

    private static bool ReadName(string s, ref int i, out string name)
    {
        i = SkipWhitespace(s, i);
        var start = i;
        while (i < s.Length && IsIdentChar(s[i]))
            i++;
        name = s[start..i];
        return name.Length > 0;
    }

    private static bool TryParseIndex(string text, out long index)
    {
        index = 0;
        if (text.Length == 0)
            return false;
        var parsed = text.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
            ? long.TryParse(text.AsSpan(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out index)
            : long.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out index);
        return parsed && index >= 0;
    }

    private static int SkipWhitespace(string s, int i)
    {
        while (i < s.Length && char.IsWhiteSpace(s[i]))
            i++;
        return i;
    }

    private static bool IsIdentChar(char c) => char.IsLetterOrDigit(c) || c == '_';
}
