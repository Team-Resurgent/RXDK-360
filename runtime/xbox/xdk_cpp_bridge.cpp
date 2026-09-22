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

/* ---- D3DXps (d3d9.a) ----------------------------------------------------- */
/* The Xbox Predicated Structure API is C++ (namespace-free free functions on
   class D3DXps / struct D3DXpsThread); clang references the Itanium names,
   d3d9.a exports the MSVC ?D3DXps_*@@ names. Forward the complete public set
   (Instancing / XPSCube / AdvancedXPS). Opaque tags: only the type NAME feeds
   the Itanium mangling, so class-vs-struct is irrelevant. */
struct D3DXps;
struct D3DXpsThread;
enum _D3DPRIMITIVETYPE  { _D3DPRIMITIVETYPE_pad = 0 };
enum _D3DFORMAT         { _D3DFORMAT_pad = 0 };

extern "C" void  xps_Initialize(D3DXps*, D3DXpsThread*)
    __asm__("?D3DXps_Initialize@@YAXPAVD3DXps@@PAUD3DXpsThread@@@Z");
void D3DXps_Initialize(D3DXps* p, D3DXpsThread* t) { xps_Initialize(p, t); }

extern "C" void  xps_Uninitialize(D3DXps*)
    __asm__("?D3DXps_Uninitialize@@YAXPAVD3DXps@@@Z");
void D3DXps_Uninitialize(D3DXps* p) { xps_Uninitialize(p); }

extern "C" void* xps_Allocate(D3DXps*, unsigned long, unsigned long)
    __asm__("?D3DXps_Allocate@@YAPAXPAVD3DXps@@KK@Z");
void* D3DXps_Allocate(D3DXps* p, unsigned long a, unsigned long b) { return xps_Allocate(p, a, b); }

extern "C" void  xps_DrawVertices(D3DXps*, _D3DPRIMITIVETYPE, unsigned long, const void*)
    __asm__("?D3DXps_DrawVertices@@YAXPAVD3DXps@@W4_D3DPRIMITIVETYPE@@KPBX@Z");
void D3DXps_DrawVertices(D3DXps* p, _D3DPRIMITIVETYPE t, unsigned long n, const void* v)
{ xps_DrawVertices(p, t, n, v); }

extern "C" void  xps_DrawIndexedVertices(D3DXps*, _D3DPRIMITIVETYPE, unsigned long, const void*, _D3DFORMAT, const void*)
    __asm__("?D3DXps_DrawIndexedVertices@@YAXPAVD3DXps@@W4_D3DPRIMITIVETYPE@@KPBXW4_D3DFORMAT@@2@Z");
void D3DXps_DrawIndexedVertices(D3DXps* p, _D3DPRIMITIVETYPE t, unsigned long n, const void* v, _D3DFORMAT f, const void* idx)
{ xps_DrawIndexedVertices(p, t, n, v, f, idx); }

extern "C" void  xps_KickOff(D3DXps*)
    __asm__("?D3DXps_KickOff@@YAXPAVD3DXps@@@Z");
void D3DXps_KickOff(D3DXps* p) { xps_KickOff(p); }

extern "C" int   xps_KickOffAndGet(D3DXps*, unsigned long*)
    __asm__("?D3DXps_KickOffAndGet@@YAHPAVD3DXps@@PAK@Z");
int D3DXps_KickOffAndGet(D3DXps* p, unsigned long* out) { return xps_KickOffAndGet(p, out); }

extern "C" int   xps_Get(D3DXps*, const void**, unsigned long*)
    __asm__("?D3DXps_Get@@YAHPAVD3DXps@@PAPBXPAK@Z");
int D3DXps_Get(D3DXps* p, const void** pp, unsigned long* out) { return xps_Get(p, pp, out); }

/* ---- XWMADecode (xwmadecode.a) ------------------------------------------ */
/* XAUDIO2 namespace free functions; xwmadecode.a exports ?...@XAUDIO2@@. */
struct tWAVEFORMATEX;
struct XWMADECODE;
struct XWMADECODE_INPUT_BUFFER_INFO;
namespace XAUDIO2 {
    extern "C" unsigned long xwma_GetRequiredBufferSize(const ::tWAVEFORMATEX*)
        __asm__("?XWMADecodeGetRequiredBufferSize@XAUDIO2@@YAKPBUtWAVEFORMATEX@@@Z");
    unsigned long XWMADecodeGetRequiredBufferSize(const ::tWAVEFORMATEX* f)
    { return xwma_GetRequiredBufferSize(f); }

    extern "C" void xwma_ProcessData(::XWMADECODE*)
        __asm__("?XWMADecodeProcessData@XAUDIO2@@YAJPAUXWMADECODE@1@@Z");
    long XWMADecodeProcessData(::XWMADECODE* d) { xwma_ProcessData(d); return 0; }

    extern "C" void xwma_Destroy(::XWMADECODE*)
        __asm__("?XWMADecodeDestroy@XAUDIO2@@YAXPAUXWMADECODE@1@@Z");
    void XWMADecodeDestroy(::XWMADECODE* d) { xwma_Destroy(d); }
}
