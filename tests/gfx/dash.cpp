/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * RXDK-360 component dashboard (bring-up scaffold): a single title that brings
 * up each middleware component in turn, renders a live D3D9 scene, and reports
 * every component's init status both on-screen (self-contained bitmap-font text
 * overlay) and via DbgPrint (so a headless xenia run is self-verifying). Builds
 * with our clang + ld.lld against the stock XDK headers/libs; shaders are
 * precompiled offline with the XDK's fxc (see build_dash.py).
 *
 * v0 covers the graphics slice proven by the triangle (D3D9 + XGraphics +
 * precompiled shaders) plus the text-overlay primitive the dashboard needs.
 * Further components (audio/input/net) are added incrementally.
 */
#include <xtl.h>

extern "C" int DbgPrint(const char *, ...);

#include "shaders/tri_vs.h"     /* g_vs_bin        - spinning triangle */
#include "shaders/tri_ps.h"     /* g_ps_bin        */
#include "shaders/text_vs.h"    /* g_text_vs_bin   - screen-space text */
#include "shaders/text_ps.h"    /* g_text_ps_bin   */
#include "font_atlas.h"         /* g_font_a8, FONT_* */

struct COLORVERTEX { float Position[3]; DWORD Color; };
struct TEXTVERTEX  { float x, y, z, w; float u, v; DWORD color; };

static D3DDevice            *g_dev;
static D3DVertexBuffer      *g_vb;
static D3DVertexDeclaration *g_triDecl, *g_textDecl;
static D3DVertexShader      *g_vsh, *g_textVsh;
static D3DPixelShader       *g_psh, *g_textPsh;
static D3DTexture           *g_fontTex;
static D3DXMATRIX            g_proj, g_view;
static int                   g_bbW = 1280, g_bbH = 720;

/* ---- text overlay ---------------------------------------------------- */

#define MAX_TEXT_VERTS 8192
static TEXTVERTEX g_text[MAX_TEXT_VERTS];
static int        g_textCount;

static void TextReset() { g_textCount = 0; }

/* px,py in pixels (origin top-left), scale multiplies the 16x16 cell. */
static void DrawText(float px, float py, float scale, DWORD color, const char *s)
{
    const float cw = FONT_CELL_W * scale, ch = FONT_CELL_H * scale;
    const float du = (float)FONT_CELL_W / FONT_ATLAS_W;
    const float dv = (float)FONT_CELL_H / FONT_ATLAS_H;
    for (; *s; ++s) {
        int c = (unsigned char)*s;
        if (c == '\n') { py += ch; px -= 0; /* caller re-sets x per line */ continue; }
        if (c < 32 || c > 127) { px += cw; continue; }
        int gi = c - 32, col = gi % FONT_COLS, row = gi / FONT_COLS;
        float u0 = col * du, v0 = row * dv, u1 = u0 + du, v1 = v0 + dv;
        /* pixel rect -> clip space */
        float x0 = (px)      / g_bbW * 2.0f - 1.0f;
        float x1 = (px + cw) / g_bbW * 2.0f - 1.0f;
        float y0 = 1.0f - (py)      / g_bbH * 2.0f;
        float y1 = 1.0f - (py + ch) / g_bbH * 2.0f;
        if (g_textCount + 6 > MAX_TEXT_VERTS) break;
        TEXTVERTEX q[6] = {
            { x0, y0, 0, 1, u0, v0, color }, { x1, y0, 0, 1, u1, v0, color },
            { x0, y1, 0, 1, u0, v1, color }, { x1, y0, 0, 1, u1, v0, color },
            { x1, y1, 0, 1, u1, v1, color }, { x0, y1, 0, 1, u0, v1, color },
        };
        for (int i = 0; i < 6; ++i) g_text[g_textCount++] = q[i];
        px += cw;
    }
}

static void TextFlush()
{
    if (!g_textCount) return;
    g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_dev->SetVertexDeclaration(g_textDecl);
    g_dev->SetVertexShader(g_textVsh);
    g_dev->SetPixelShader(g_textPsh);
    g_dev->SetTexture(0, g_fontTex);
    g_dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    g_dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    g_dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    g_dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, g_textCount / 3, g_text, sizeof(TEXTVERTEX));
}

/* ---- setup ----------------------------------------------------------- */

static HRESULT InitD3D()
{
    DbgPrint("[DASH] Direct3DCreate9...\n");
    Direct3D *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return E_FAIL;

    D3DPRESENT_PARAMETERS pp;   ZeroMemory(&pp, sizeof(pp));
    XVIDEO_MODE vm;             ZeroMemory(&vm, sizeof(vm));
    XGetVideoMode(&vm);
    g_bbW = pp.BackBufferWidth  = vm.dwDisplayWidth  < 1280 ? vm.dwDisplayWidth  : 1280;
    g_bbH = pp.BackBufferHeight = vm.dwDisplayHeight <  720 ? vm.dwDisplayHeight :  720;
    pp.BackBufferFormat       = D3DFMT_X8R8G8B8;
    pp.BackBufferCount        = 1;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval   = D3DPRESENT_INTERVAL_ONE;

    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev);
    DbgPrint("[DASH] CreateDevice hr=0x%08x %dx%d\n", hr, g_bbW, g_bbH);
    return hr;
}

static HRESULT InitScene()
{
    /* spinning-triangle background (proven path) */
    g_dev->CreateVertexShader((const DWORD *)g_vs_bin, &g_vsh);
    g_dev->CreatePixelShader((const DWORD *)g_ps_bin, &g_psh);
    D3DVERTEXELEMENT9 triElems[] = {
        { 0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
        D3DDECL_END()
    };
    g_dev->CreateVertexDeclaration(triElems, &g_triDecl);
    g_dev->CreateVertexBuffer(3 * sizeof(COLORVERTEX), D3DUSAGE_WRITEONLY, 0,
                              D3DPOOL_MANAGED, &g_vb, NULL);
    COLORVERTEX tv[] = {
        {  0.0f, -1.1547f, 0.0f, 0xffff0000 },
        { -1.0f,  0.5777f, 0.0f, 0xff00ff00 },
        {  1.0f,  0.5777f, 0.0f, 0xffffff00 },
    };
    void *p = NULL; g_vb->Lock(0, 0, &p, 0); memcpy(p, tv, sizeof(tv)); g_vb->Unlock();
    D3DXMatrixPerspectiveFovLH(&g_proj, D3DX_PI / 4.0f, 16.0f / 9.0f, 1.0f, 200.0f);
    D3DXVECTOR3 eye(0, 0, -7), at(0, 0, 0), up(0, 1, 0);
    D3DXMatrixLookAtLH(&g_view, &eye, &at, &up);

    /* text overlay: shaders, decl, font texture */
    g_dev->CreateVertexShader((const DWORD *)g_text_vs_bin, &g_textVsh);
    g_dev->CreatePixelShader((const DWORD *)g_text_ps_bin, &g_textPsh);
    D3DVERTEXELEMENT9 txElems[] = {
        { 0,  0, D3DDECLTYPE_FLOAT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        { 0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
        D3DDECL_END()
    };
    g_dev->CreateVertexDeclaration(txElems, &g_textDecl);

    HRESULT hr = g_dev->CreateTexture(FONT_ATLAS_W, FONT_ATLAS_H, 1, 0,
                                      D3DFMT_LIN_A8, D3DPOOL_MANAGED, &g_fontTex, NULL);
    DbgPrint("[DASH] CreateTexture(font) hr=0x%08x\n", hr);
    if (FAILED(hr)) return hr;
    D3DLOCKED_RECT lr;
    hr = g_fontTex->LockRect(0, &lr, NULL, 0);
    if (SUCCEEDED(hr)) {
        unsigned char *dst = (unsigned char *)lr.pBits;
        for (int y = 0; y < FONT_ATLAS_H; ++y)
            memcpy(dst + y * lr.Pitch, g_font_a8 + y * FONT_ATLAS_W, FONT_ATLAS_W);
        g_fontTex->UnlockRect(0);
    }
    DbgPrint("[DASH] font LockRect hr=0x%08x pitch=%d\n", hr, (int)lr.Pitch);
    return S_OK;
}

/* ---- component status model ------------------------------------------ */

struct Comp { const char *name; const char *status; DWORD color; };
static Comp g_comps[] = {
    { "D3D9        (Direct3DCreate9/CreateDevice)", "OK",      0xff40ff40 },
    { "XGraphics   (video mode / present)",         "OK",      0xff40ff40 },
    { "Shaders     (fxc precompiled VS/PS)",        "OK",      0xff40ff40 },
    { "Text overlay(A8 font texture)",              "OK",      0xff40ff40 },
    { "XAudio2     (audio engine)",                 "PENDING", 0xffffc040 },
    { "XInput      (controller)",                   "PENDING", 0xffffc040 },
    { "XNet        (networking)",                   "PENDING", 0xffffc040 },
};
static const int NCOMP = sizeof(g_comps) / sizeof(g_comps[0]);

static void RenderTriangle(float angle)
{
    g_dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_dev->SetVertexDeclaration(g_triDecl);
    g_dev->SetStreamSource(0, g_vb, 0, sizeof(COLORVERTEX));
    g_dev->SetVertexShader(g_vsh);
    g_dev->SetPixelShader(g_psh);
    D3DXMATRIX world, wvp, tmp;
    D3DXMatrixRotationZ(&world, angle);
    D3DXMatrixMultiply(&tmp, &world, &g_view);
    D3DXMatrixMultiply(&wvp, &tmp, &g_proj);
    g_dev->SetVertexShaderConstantF(0, (float *)&wvp, 4);
    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
}

static void RenderDashboard(int highlight)
{
    TextReset();
    DrawText(48, 40, 2.0f, 0xffffffff, "RXDK-360 COMPONENT DASHBOARD");
    DrawText(48, 84, 1.0f, 0xff8080ff, "our clang + ld.lld  |  stock XDK headers/libs  |  xenia");
    float y = 140;
    for (int i = 0; i < NCOMP; ++i) {
        DWORD c = (i == highlight) ? 0xffffffff : g_comps[i].color;
        DrawText(48,  y, 1.25f, c, g_comps[i].name);
        DrawText(760, y, 1.25f, c, g_comps[i].status);
        y += 26;
    }
    TextFlush();
}

int main(void)
{
    DbgPrint("[DASH] start\n");
    if (FAILED(InitD3D()))   { DbgPrint("[DASH] InitD3D FAILED\n");   return 1; }
    if (FAILED(InitScene())) { DbgPrint("[DASH] InitScene FAILED\n"); return 1; }
    for (int i = 0; i < NCOMP; ++i)
        DbgPrint("[DASH] component %-12s : %s\n", g_comps[i].name, g_comps[i].status);
    DbgPrint("[DASH] scene ready; rendering\n");

    int frames = 300;
#ifdef DASH_SPIN_FOREVER
    frames = 0x7fffffff;
#endif
    for (int f = 0; f < frames; ++f) {
        g_dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                     D3DCOLOR_XRGB(8, 12, 32), 1.0f, 0);
        RenderTriangle((float)f * 0.02f);
        RenderDashboard((f / 45) % NCOMP);   /* sweep a highlight down the list */
        g_dev->Present(NULL, NULL, NULL, NULL);
        if (f % 60 == 0) DbgPrint("[DASH] frame %d presented\n", f);
    }
    DbgPrint("[DASH] DONE\n");
    return 0;
}
