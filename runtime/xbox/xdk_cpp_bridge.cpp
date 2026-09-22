/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RXDK-360 XDK C++ ABI bridge.
 *
 * Several XDK APIs (XJSON, ASF, XAV, QNet, ...) are C++ (not extern "C"), so
 * clang references them with Itanium mangling while the MSVC-built libraries
 * export the MSVC name - the reference goes unresolved. Bridge each referenced
 * routine with a thin forwarder whose C++ signature reproduces the exact Itanium
 * name clang emits, tail-calling the MSVC symbol through an asm label. Being in
 * libc.a a forwarder is pulled only when a title references the routine, and the
 * real library member (xjson.a / xav.a / qnetxaudio2.a) is then pulled to
 * satisfy the MSVC symbol. Companion to fxl_bridge.cpp (FXLSetShaders).
 *
 * Only the type NAMES feed Itanium mangling, so opaque tags suffice; the
 * struct-vs-enum/class distinction does not affect the mangled name.
 */

struct HJSONREADER__;
struct _JSONTokenType;
struct IAsfWriter;
struct XAVInit;
struct IXAVPlayer;
struct IXAVPlayerEvents;
struct IXAudio2;
struct IXAVLicenseAcquisition;
struct IQNetCallbacks;
struct HWND__;
struct IQNet;
enum   _QNET_SESSIONTYPE { _QNET_SESSIONTYPE_pad = 0 };

/* ---- XJSON (xjson.a) ---------------------------------------------------- */

extern "C" HJSONREADER__* xjson_CreateReader()
    __asm__("?XJSONCreateReader@@YAPAUHJSONREADER__@@XZ");
HJSONREADER__* XJSONCreateReader() { return xjson_CreateReader(); }

extern "C" long xjson_SetBuffer(HJSONREADER__*, const char*, unsigned long, int)
    __asm__("?XJSONSetBuffer@@YAJPAUHJSONREADER__@@PBDKH@Z");
long XJSONSetBuffer(HJSONREADER__* r, const char* b, unsigned long n, int f)
{ return xjson_SetBuffer(r, b, n, f); }

extern "C" long xjson_ReadToken(HJSONREADER__*, _JSONTokenType*, unsigned long*, unsigned long*)
    __asm__("?XJSONReadToken@@YAJPAUHJSONREADER__@@PAW4_JSONTokenType@@PAK2@Z");
long XJSONReadToken(HJSONREADER__* r, _JSONTokenType* t, unsigned long* a, unsigned long* b)
{ return xjson_ReadToken(r, t, a, b); }

extern "C" long xjson_GetTokenValueW(HJSONREADER__*, wchar_t*, unsigned long)
    __asm__("?XJSONGetTokenValue@@YAJPAUHJSONREADER__@@PA_WK@Z");
long XJSONGetTokenValue(HJSONREADER__* r, wchar_t* b, unsigned long n)
{ return xjson_GetTokenValueW(r, b, n); }

extern "C" long xjson_CloseReader(HJSONREADER__*)
    __asm__("?XJSONCloseReader@@YAJPAUHJSONREADER__@@@Z");
long XJSONCloseReader(HJSONREADER__* r) { return xjson_CloseReader(r); }

/* ---- ASF writer (xav.a) ------------------------------------------------- */

extern "C" long asf_CreateAsfWriter(const char*, IAsfWriter**)
    __asm__("?CreateAsfWriter@@YAJPBDPAPAUIAsfWriter@@@Z");
long CreateAsfWriter(const char* s, IAsfWriter** w) { return asf_CreateAsfWriter(s, w); }

extern "C" long asf_DestroyAsfWriter(IAsfWriter*)
    __asm__("?DestroyAsfWriter@@YAJPAUIAsfWriter@@@Z");
long DestroyAsfWriter(IAsfWriter* w) { return asf_DestroyAsfWriter(w); }

/* ---- XAV (xav.a) -------------------------------------------------------- */

extern "C" long xav_XAVInitialize(const XAVInit*, unsigned long*)
    __asm__("?XAVInitialize@@YAJPBUXAVInit@@QAK@Z");
long XAVInitialize(const XAVInit* i, unsigned long* p) { return xav_XAVInitialize(i, p); }

extern "C" long xav_CreateXAVPlayer(IXAVPlayer**, IXAVPlayerEvents*, IXAudio2*)
    __asm__("?CreateXAVPlayer@@YAJPAPAUIXAVPlayer@@PAUIXAVPlayerEvents@@PAUIXAudio2@@@Z");
long CreateXAVPlayer(IXAVPlayer** p, IXAVPlayerEvents* e, IXAudio2* a)
{ return xav_CreateXAVPlayer(p, e, a); }

extern "C" long xav_CreateXAVLicenseAcquisition(IXAVLicenseAcquisition**)
    __asm__("?CreateXAVLicenseAcquisition@@YAJPAPAUIXAVLicenseAcquisition@@@Z");
long CreateXAVLicenseAcquisition(IXAVLicenseAcquisition** p)
{ return xav_CreateXAVLicenseAcquisition(p); }

struct IUnknown;
extern "C" long xav_CreateXAVLicenseAcquisitionU(IUnknown*, IXAVLicenseAcquisition**)
    __asm__("?CreateXAVLicenseAcquisition@@YAJPAUIUnknown@@PAPAUIXAVLicenseAcquisition@@@Z");
long CreateXAVLicenseAcquisition(IUnknown* u, IXAVLicenseAcquisition** p)
{ return xav_CreateXAVLicenseAcquisitionU(u, p); }

extern "C" long xav_XAVShutdown()   __asm__("?XAVShutdown@@YAJXZ");
long XAVShutdown()   { return xav_XAVShutdown(); }
extern "C" long xav_XAVPreRender()  __asm__("?XAVPreRender@@YAJXZ");
long XAVPreRender()  { return xav_XAVPreRender(); }
extern "C" long xav_XAVPostRender() __asm__("?XAVPostRender@@YAJXZ");
long XAVPostRender() { return xav_XAVPostRender(); }

/* ---- QNet (qnetxaudio2.a) ----------------------------------------------- */

extern "C" long qnet_CreateUsingXAudio2(_QNET_SESSIONTYPE, IQNetCallbacks*, HWND__*, IXAudio2*, IQNet**)
    __asm__("?QNetCreateUsingXAudio2@@YAJW4_QNET_SESSIONTYPE@@PAVIQNetCallbacks@@PAUHWND__@@PAUIXAudio2@@PAPAVIQNet@@@Z");
long QNetCreateUsingXAudio2(_QNET_SESSIONTYPE t, IQNetCallbacks* c, HWND__* h, IXAudio2* a, IQNet** q)
{ return qnet_CreateUsingXAudio2(t, c, h, a, q); }

/* ---- NUI (nuiapi.a) ----------------------------------------------------- */
/* __vector4/_XMMATRIX need real layouts: NuiTransformMatrixLevel takes a
   __vector4 by value and returns a 64-byte _XMMATRIX (sret). */
struct __attribute__((aligned(16))) __vector4 { unsigned u[4]; };
struct _XMMATRIX { __vector4 r[4]; };
struct _NUI_SKELETON_FRAME;
struct _NUI_TRANSFORM_SMOOTH_PARAMETERS;

extern "C" long nui_TransformSmooth(_NUI_SKELETON_FRAME*, const _NUI_TRANSFORM_SMOOTH_PARAMETERS*)
    __asm__("?NuiTransformSmooth@@YAJPAU_NUI_SKELETON_FRAME@@PBU_NUI_TRANSFORM_SMOOTH_PARAMETERS@@@Z");
long NuiTransformSmooth(_NUI_SKELETON_FRAME* f, const _NUI_TRANSFORM_SMOOTH_PARAMETERS* p)
{ return nui_TransformSmooth(f, p); }

extern "C" _XMMATRIX nui_TransformMatrixLevel(__vector4)
    __asm__("?NuiTransformMatrixLevel@@YA?AU_XMMATRIX@@U__vector4@@@Z");
_XMMATRIX NuiTransformMatrixLevel(__vector4 v) { return nui_TransformMatrixLevel(v); }
