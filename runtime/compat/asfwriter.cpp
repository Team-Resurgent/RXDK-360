// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// RXDK-360 compat: AsfWriterPropertyValue, the small property-value helper the
// ASF writer API (asfwriterapi.h) declares out-of-line. Like the XAPO base
// classes, the XDK ships it only in a prebuilt MSVC library, so a clang title
// that constructs one (ASFWriter sample) needs the Itanium-mangled ctor/Clear.
// The class has no vtable/RTTI - just a type tag and a value union to reset.
#include <xtl.h>              // Win32/Xbox base types (DWORD/LONG/WORD) asfwriterapi.h assumes
#include <asfwriterapi.h>
#include <string.h>

AsfWriterPropertyValue::AsfWriterPropertyValue()
{
    Clear();
}

VOID AsfWriterPropertyValue::Clear()
{
    m_Type = NotUsedProperty;
    memset(&m_Value, 0, sizeof(m_Value));
}
