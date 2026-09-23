// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// RXDK-360 compat library: an Itanium-ABI anchor for the XDK's IXuiFontRenderer
// interface (xuirender.h). A title that installs a custom font renderer subclasses
// IXuiFontRenderer (the XuiFontRenderer sample's MyXuiFontRenderer) and passes it
// to XuiFontSetRenderer. Because the sample is compiled by clang, its derived class
// emits Itanium RTTI whose base record references `_ZTI16IXuiFontRenderer` (typeinfo
// for IXuiFontRenderer).
//
// IXuiFontRenderer's methods are declared (via DECLARE_INTERFACE) but NOT marked
// PURE, so the interface has a *key function* (its first virtual, Init). Under the
// Itanium ABI clang emits the interface's vtable+RTTI only in the translation unit
// that defines that key function -- which, in the stock XDK, is the MSVC-ABI xui
// lib, so no Itanium `_ZTI16IXuiFontRenderer` exists and the title fails to link.
//
// We supply it here, with clang: defining the interface's virtuals out of line
// anchors `_ZTV16IXuiFontRenderer` + `_ZTI16IXuiFontRenderer` under the Itanium ABI
// in the compat library the title links. The bodies are never reached -- a title
// only ever holds a *derived* renderer, which overrides every method -- they exist
// solely to make the base vtable well-formed so its typeinfo can be emitted.
#include <xtl.h>
#include <xui.h>

HRESULT STDMETHODCALLTYPE IXuiFontRenderer::Init( float /*fDpi*/ )
{ return E_NOTIMPL; }

VOID STDMETHODCALLTYPE IXuiFontRenderer::Term()
{ }

HRESULT STDMETHODCALLTYPE IXuiFontRenderer::GetCaps( DWORD* /*pdwCaps*/ )
{ return E_NOTIMPL; }

HRESULT STDMETHODCALLTYPE IXuiFontRenderer::CreateFont(
    const TypefaceDescriptor* /*pTypefaceDescriptor*/, float /*fPointSize*/,
    DWORD /*dwStyle*/, DWORD /*dwReserved*/, HFONTOBJ* /*phFont*/ )
{ return E_NOTIMPL; }

VOID STDMETHODCALLTYPE IXuiFontRenderer::ReleaseFont( HFONTOBJ /*hFont*/ )
{ }

HRESULT STDMETHODCALLTYPE IXuiFontRenderer::GetFontMetrics(
    HFONTOBJ /*hFont*/, XUIFontMetrics* /*pFontMetrics*/ )
{ return E_NOTIMPL; }

HRESULT STDMETHODCALLTYPE IXuiFontRenderer::GetCharMetrics(
    HFONTOBJ /*hFont*/, WCHAR /*wch*/, XUICharMetrics* /*pCharMetrics*/ )
{ return E_NOTIMPL; }

HRESULT STDMETHODCALLTYPE IXuiFontRenderer::DrawCharToTexture(
    HFONTOBJ /*hFont*/, WCHAR /*wch*/, HXUIDC /*hDC*/, IXuiTexture* /*pTexture*/,
    UINT /*x*/, UINT /*y*/, UINT /*width*/, UINT /*height*/,
    UINT /*insetX*/, UINT /*insetY*/ )
{ return E_NOTIMPL; }

HRESULT STDMETHODCALLTYPE IXuiFontRenderer::DrawCharsToDevice(
    HFONTOBJ /*hFont*/, CharData* /*pCharData*/, DWORD /*dwCount*/,
    RECT* /*pClipRect*/, HXUIDC /*hDC*/, D3DXMATRIX* /*pWorldViewProj*/ )
{ return E_NOTIMPL; }
