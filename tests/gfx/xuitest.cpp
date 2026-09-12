/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * Minimal standalone XUI repro (like xmvplay.cpp): bring up a D3D device, share
 * it with the XUI render library, register a TTF typeface, create a font and
 * draw immediate-mode text for a few frames.
 *
 *   font staged next to the xex at game:\Media\Xui\xarialuni.ttf
 *
 * Status in xenia (as of the save/rest JIT fix): the render layer comes up
 * cleanly under our toolchain -- XuiRenderInitShared and XuiRenderCreateDC
 * both return S_OK against our own D3D device -- but XuiInit then faults deep
 * inside XUI's private free-list allocator (SplitFreeBlock): it walks a heap
 * free-block whose `next` link reads back NULL and dereferences it (guest EA 4,
 * i.e. host membase + 4). XUI's internal heap terminates its free list
 * differently than it expects under xenia's memory layout; XUI exposes no hook
 * to supply the pool, and it is stock (sourceless) MS code, so this is a xenia
 * environment gap rather than a toolchain defect. Kept as the XUI link/init
 * repro -- it proves xui{run,render}.lib link and the render layer initialises,
 * and pins the exact stop point for the eventual hardware pass (where XUI's
 * own heap init is expected to behave). NOT wired into dash.cpp: a fault would
 * abort the auto-cycling self-test.
 */
#include <xtl.h>
#include <xui.h>

extern "C" int DbgPrint(const char *, ...);

static D3DDevice           *g_dev;
static D3DPRESENT_PARAMETERS g_pp;
static HXUIDC               g_hDC;
static HXUIFONT             g_hFont;

static HRESULT InitD3D()
{
    Direct3D *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return E_FAIL;
    ZeroMemory(&g_pp, sizeof(g_pp));
    XVIDEO_MODE vm; ZeroMemory(&vm, sizeof(vm)); XGetVideoMode(&vm);
    g_pp.BackBufferWidth  = vm.dwDisplayWidth  < 1280 ? vm.dwDisplayWidth  : 1280;
    g_pp.BackBufferHeight = vm.dwDisplayHeight <  720 ? vm.dwDisplayHeight :  720;
    g_pp.BackBufferFormat = D3DFMT_X8R8G8B8; g_pp.BackBufferCount = 1;
    g_pp.EnableAutoDepthStencil = TRUE; g_pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    g_pp.SwapEffect = D3DSWAPEFFECT_DISCARD; g_pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_pp, &g_dev);
    DbgPrint("[XUI] CreateDevice hr=0x%08x %dx%d\n", hr, g_pp.BackBufferWidth, g_pp.BackBufferHeight);
    return hr;
}

static HRESULT InitXui()
{
    XUIInitParams initparams = { 0 };
    XUI_INIT_PARAMS(initparams);

    TypefaceDescriptor desc = { 0 };
    desc.szTypeface = L"Arial Unicode MS";
    desc.szLocator  = L"file://game:/Media/Xui/xarialuni.ttf";

    HRESULT hr = XuiRenderInitShared(g_dev, &g_pp, XuiD3DXTextureLoader);
    DbgPrint("[XUI] XuiRenderInitShared hr=0x%08x\n", hr);
    if (FAILED(hr)) return hr;

    hr = XuiRenderCreateDC(&g_hDC);
    DbgPrint("[XUI] XuiRenderCreateDC hr=0x%08x\n", hr);
    if (FAILED(hr)) return hr;

    hr = XuiInit(&initparams);
    DbgPrint("[XUI] XuiInit hr=0x%08x\n", hr);
    if (FAILED(hr)) return hr;

    hr = XuiRegisterTypeface(&desc, TRUE);
    DbgPrint("[XUI] XuiRegisterTypeface hr=0x%08x\n", hr);
    if (FAILED(hr)) return hr;

    hr = XuiCreateFont(L"Arial Unicode MS", 28.0f, XUI_FONT_STYLE_NORMAL, 0, &g_hFont);
    DbgPrint("[XUI] XuiCreateFont hr=0x%08x font=%p\n", hr, g_hFont);
    return hr;
}

static void DrawStr(D3DCOLOR color, float x, float y, LPCWSTR text)
{
    XUIRect clip(0, 0, (float)g_pp.BackBufferWidth - x, (float)g_pp.BackBufferHeight - y);
    XuiMeasureText(g_hFont, text, -1, XUI_FONT_STYLE_NORMAL, 0, &clip);
    D3DXMATRIX m; D3DXMatrixIdentity(&m); m._41 = x; m._42 = y;
    XuiRenderSetTransform(g_hDC, &m);
    XuiSelectFont(g_hDC, g_hFont);
    XuiSetColorFactor(g_hDC, (DWORD)color);
    XuiDrawText(g_hDC, text, XUI_FONT_STYLE_NORMAL, 0, &clip);
}

int main(void)
{
    DbgPrint("[XUI] start\n");
    if (FAILED(InitD3D())) { DbgPrint("[XUI] InitD3D FAILED\n"); return 1; }
    if (FAILED(InitXui())) { DbgPrint("[XUI] InitXui FAILED\n"); return 1; }

    for (int f = 0; f < 1800; ++f) {   /* ~30s at 60fps so it's watchable */
        g_dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(8, 10, 26), 1.0f, 0);
        XuiRenderBegin(g_hDC, D3DCOLOR_ARGB(255, 8, 10, 26));
        D3DXMATRIX v; D3DXMatrixIdentity(&v);
        XuiRenderSetViewTransform(g_hDC, &v);
        DrawStr(D3DCOLOR_ARGB(255, 255, 255, 255), 200, 200, L"RXDK-360 XUI immediate text");
        DrawStr(D3DCOLOR_ARGB(255, 120, 200, 255), 200, 260, L"XuiCreateFont + XuiDrawText");
        XuiRenderEnd(g_hDC);
        XuiRenderPresent(g_hDC, NULL, NULL, NULL);
        g_dev->Present(NULL, NULL, NULL, NULL);
        if (f == 0) DbgPrint("[XUI] first frame presented\n");
    }
    DbgPrint("[XUI] DONE\n");
    return 0;
}
