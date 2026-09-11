/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * RXDK-360 component test harness: one title that steps through a sequence of
 * per-component test sections, each of which actively exercises just that
 * component while it is on screen (enter -> update -> exit) and reports its
 * result both on-screen and via DbgPrint -- so a headless xenia run is a
 * self-verifying, per-component test log, and a windowed run is a live demo.
 *
 * Built with our clang + ld.lld against the stock XDK headers/libs; every shader
 * is precompiled offline with the XDK's fxc (see build_dash.py).
 *
 * Sections: D3D9 core / Shaders (cycles effects) / Text+font / XAudio2 (tone on
 * enter, off on exit) / XInput (live pad) / XNet (startup + address/link query).
 * Sections auto-cycle (~5s each); a controller (or xenia's keyboard mapping) can
 * step/pause them, with a small "next in N" auto-advance countdown.
 */
#include <xtl.h>
#include <xinputdefs.h>
#include <xaudio2.h>
#include <winsockx.h>
#include <math.h>

extern "C" int DbgPrint(const char *, ...);

#include "shaders/tri_vs.h"        /* g_vs_bin        - triangle */
#include "shaders/tri_ps.h"        /* g_ps_bin        */
#include "shaders/text_vs.h"       /* g_text_vs_bin   - screen quad / text */
#include "shaders/text_ps.h"       /* g_text_ps_bin   */
#include "shaders/fx_gradient_ps.h"/* g_gradient_bin  - effect PS (fullscreen) */
#include "shaders/fx_plasma_ps.h"  /* g_plasma_bin    */
#include "shaders/fx_rings_ps.h"   /* g_rings_bin     */
#include "font_atlas.h"            /* g_font_a8, FONT_* */

/* ---- colours --------------------------------------------------------- */
#define COL_OK      0xff40ff40
#define COL_PEND    0xffffc040
#define COL_FAIL    0xffff4040
#define COL_WHITE   0xffffffff
#define COL_BLUE    0xff6c8cff
#define COL_DIM     0xff9090a0

struct COLORVERTEX { float Position[3]; DWORD Color; };
struct TEXTVERTEX  { float x, y, z, w; float u, v; DWORD color; };

static D3DDevice            *g_dev;
static int                   g_bbW = 1280, g_bbH = 720;

/* triangle */
static D3DVertexBuffer      *g_vb;
static D3DVertexDeclaration *g_triDecl, *g_textDecl;
static D3DVertexShader      *g_vsh, *g_textVsh;
static D3DPixelShader       *g_psh, *g_textPsh;
static D3DXMATRIX            g_proj, g_view;

/* text overlay */
static D3DTexture           *g_fontTex;
#define MAX_TEXT_VERTS 12288
static TEXTVERTEX            g_text[MAX_TEXT_VERTS];
static int                   g_textCount;

/* fullscreen effect pixel shaders */
static D3DPixelShader       *g_fx[3];
static const char           *g_fxName[3] = { "gradient", "plasma", "rings" };

/* audio */
static IXAudio2               *g_xa2;
static IXAudio2MasteringVoice *g_master;
static IXAudio2SourceVoice    *g_srcVoice;
static bool                    g_audioReady, g_tonePlaying;
static short                   g_tone[44100];

/* net */
static bool                    g_netStarted;

/* ============================ text overlay ============================ */

#define FONT_ADVANCE 0.52f
static void TextReset() { g_textCount = 0; }

static float DrawText(float px, float py, float scale, DWORD color, const char *s)
{
    const float cw = FONT_CELL_W * scale, ch = FONT_CELL_H * scale, adv = cw * FONT_ADVANCE;
    const float du = (float)FONT_CELL_W / FONT_ATLAS_W, dv = (float)FONT_CELL_H / FONT_ATLAS_H;
    const float hx = 0.5f / FONT_ATLAS_W, hy = 0.5f / FONT_ATLAS_H;
    for (; *s; ++s) {
        int c = (unsigned char)*s;
        if (c < 32 || c > 127) { px += adv; continue; }
        int gi = c - 32, col = gi % FONT_COLS, row = gi / FONT_COLS;
        float u0 = col * du + hx, v0 = row * dv + hy, u1 = (col + 1) * du - hx, v1 = (row + 1) * dv - hy;
        float x0 = px / g_bbW * 2.f - 1.f, x1 = (px + cw) / g_bbW * 2.f - 1.f;
        float y0 = 1.f - py / g_bbH * 2.f, y1 = 1.f - (py + ch) / g_bbH * 2.f;
        if (g_textCount + 6 > MAX_TEXT_VERTS) break;
        TEXTVERTEX q[6] = {
            { x0, y0, 0, 1, u0, v0, color }, { x1, y0, 0, 1, u1, v0, color },
            { x0, y1, 0, 1, u0, v1, color }, { x1, y0, 0, 1, u1, v0, color },
            { x1, y1, 0, 1, u1, v1, color }, { x0, y1, 0, 1, u0, v1, color },
        };
        for (int i = 0; i < 6; ++i) g_text[g_textCount++] = q[i];
        px += adv;
    }
    return px;
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
    g_textCount = 0;
}

/* centred text: returns start x for a given string at a given scale */
static float CenterX(const char *s, float scale)
{
    int n = 0; for (const char *p = s; *p; ++p) ++n;
    float w = n * FONT_CELL_W * scale * FONT_ADVANCE;
    return (g_bbW - w) * 0.5f;
}

/* ============================ setup =================================== */

static HRESULT InitD3D()
{
    Direct3D *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return E_FAIL;
    D3DPRESENT_PARAMETERS pp; ZeroMemory(&pp, sizeof(pp));
    XVIDEO_MODE vm; ZeroMemory(&vm, sizeof(vm)); XGetVideoMode(&vm);
    g_bbW = pp.BackBufferWidth  = vm.dwDisplayWidth  < 1280 ? vm.dwDisplayWidth  : 1280;
    g_bbH = pp.BackBufferHeight = vm.dwDisplayHeight <  720 ? vm.dwDisplayHeight :  720;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev);
    DbgPrint("[DASH] CreateDevice hr=0x%08x %dx%d\n", hr, g_bbW, g_bbH);
    return hr;
}

static HRESULT InitText()
{
    g_dev->CreateVertexShader((const DWORD *)g_text_vs_bin, &g_textVsh);
    g_dev->CreatePixelShader((const DWORD *)g_text_ps_bin, &g_textPsh);
    D3DVERTEXELEMENT9 e[] = {
        { 0,  0, D3DDECLTYPE_FLOAT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        { 0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
        D3DDECL_END()
    };
    g_dev->CreateVertexDeclaration(e, &g_textDecl);
    HRESULT hr = g_dev->CreateTexture(FONT_ATLAS_W, FONT_ATLAS_H, 1, 0,
                                      D3DFMT_LIN_A8, D3DPOOL_MANAGED, &g_fontTex, NULL);
    if (FAILED(hr)) { DbgPrint("[DASH] font CreateTexture hr=0x%08x\n", hr); return hr; }
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(g_fontTex->LockRect(0, &lr, NULL, 0))) {
        for (int y = 0; y < FONT_ATLAS_H; ++y)
            memcpy((BYTE *)lr.pBits + y * lr.Pitch, g_font_a8 + y * FONT_ATLAS_W, FONT_ATLAS_W);
        g_fontTex->UnlockRect(0);
    }
    return S_OK;
}

static void InitTriangle()
{
    g_dev->CreateVertexShader((const DWORD *)g_vs_bin, &g_vsh);
    g_dev->CreatePixelShader((const DWORD *)g_ps_bin, &g_psh);
    D3DVERTEXELEMENT9 e[] = {
        { 0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
        D3DDECL_END()
    };
    g_dev->CreateVertexDeclaration(e, &g_triDecl);
    g_dev->CreateVertexBuffer(3 * sizeof(COLORVERTEX), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &g_vb, NULL);
    COLORVERTEX v[] = {
        {  0.0f, -1.1547f, 0.0f, 0xffff0000 },
        { -1.0f,  0.5777f, 0.0f, 0xff00ff00 },
        {  1.0f,  0.5777f, 0.0f, 0xffffff00 },
    };
    void *p = NULL; g_vb->Lock(0, 0, &p, 0); memcpy(p, v, sizeof(v)); g_vb->Unlock();
    D3DXMatrixPerspectiveFovLH(&g_proj, D3DX_PI / 4.0f, 16.0f / 9.0f, 1.0f, 200.0f);
    D3DXVECTOR3 eye(0, 0, -4.2f), at(0, 0, 0), up(0, 1, 0);
    D3DXMatrixLookAtLH(&g_view, &eye, &at, &up);
}

static void InitFx()
{
    g_dev->CreatePixelShader((const DWORD *)g_gradient_bin, &g_fx[0]);
    g_dev->CreatePixelShader((const DWORD *)g_plasma_bin,   &g_fx[1]);
    g_dev->CreatePixelShader((const DWORD *)g_rings_bin,    &g_fx[2]);
}

static void InitAudioEngine()
{
    HRESULT hr = XAudio2Create(&g_xa2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    DbgPrint("[DASH] XAudio2Create hr=0x%08x\n", hr);
    if (FAILED(hr) || !g_xa2) return;
    hr = g_xa2->CreateMasteringVoice(&g_master, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0, 0, NULL);
    if (FAILED(hr)) { DbgPrint("[DASH] master hr=0x%08x\n", hr); return; }
    for (int i = 0; i < 44100; ++i)
        g_tone[i] = (short)(0.28 * sin(2.0 * 3.14159265 * 440.0 * i / 44100.0) * 32767.0);
    WAVEFORMATEX wf; ZeroMemory(&wf, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1; wf.nSamplesPerSec = 44100;
    wf.wBitsPerSample = 16; wf.nBlockAlign = 2; wf.nAvgBytesPerSec = 88200;
    hr = g_xa2->CreateSourceVoice(&g_srcVoice, &wf, 0, 4.0f, NULL, NULL, NULL);
    if (FAILED(hr)) { DbgPrint("[DASH] source voice hr=0x%08x\n", hr); return; }
    XAUDIO2_BUFFER b; ZeroMemory(&b, sizeof(b));
    b.AudioBytes = sizeof(g_tone); b.pAudioData = (const BYTE *)g_tone;
    b.Flags = XAUDIO2_END_OF_STREAM; b.LoopCount = XAUDIO2_LOOP_INFINITE;
    g_srcVoice->SubmitSourceBuffer(&b, NULL);
    g_audioReady = true;
    DbgPrint("[DASH] audio engine ready\n");
}

static void ToneStart() { if (g_audioReady && !g_tonePlaying) { g_srcVoice->Start(0, XAUDIO2_COMMIT_NOW); g_tonePlaying = true; } }
static void ToneStop()  { if (g_audioReady &&  g_tonePlaying) { g_srcVoice->Stop(0, XAUDIO2_COMMIT_NOW); g_tonePlaying = false; } }

static void InitNet()
{
    XNetStartupParams xnsp; ZeroMemory(&xnsp, sizeof(xnsp));
    xnsp.cfgSizeOfStruct = sizeof(xnsp);
    INT r = XNetStartup(&xnsp);   /* address acquisition is async after this */
    g_netStarted = (r == 0);
    DbgPrint("[DASH] XNetStartup r=%d\n", r);
}

/* ============================ sections =============================== */

#define AUTO_FRAMES 300   /* ~5s per section at 60fps */

enum { SEC_D3D9, SEC_SHADERS, SEC_TEXT, SEC_AUDIO, SEC_INPUT, SEC_NET, SEC_COUNT };
static const char *g_secName[SEC_COUNT] = {
    "D3D9 CORE", "SHADERS", "TEXT / FONT", "XAUDIO2", "XINPUT", "XNET",
};
static int   g_section, g_frameInSec, g_totalFrames, g_cycles;
static bool  g_paused;
static char  g_status[128];
static DWORD g_statusCol = COL_OK;
static int   g_netTried;

static void SetStatus(DWORD col, const char *s) { g_statusCol = col; int i = 0; for (; s[i] && i < 127; ++i) g_status[i] = s[i]; g_status[i] = 0; }

static void SectionEnter(int s)
{
    g_frameInSec = 0;
    DbgPrint("[DASH] === section %d/%d: %s ===\n", s + 1, SEC_COUNT, g_secName[s]);
    switch (s) {
    case SEC_D3D9:    SetStatus(COL_OK, "device HAL 1280x720 X8R8G8B8 / D24S8 -- drawing"); break;
    case SEC_SHADERS: SetStatus(COL_OK, "cycling precompiled PS effects"); break;
    case SEC_TEXT:    SetStatus(COL_OK, "A8 atlas font, screen-space quads"); break;
    case SEC_AUDIO:   ToneStart();
                      SetStatus(g_audioReady ? COL_OK : COL_FAIL,
                                g_audioReady ? "440Hz voice playing (stops on exit)" : "engine unavailable");
                      DbgPrint("[DASH] audio: tone %s\n", g_audioReady ? "started" : "unavailable"); break;
    case SEC_INPUT:   SetStatus(COL_PEND, "polling XInput port 0..."); break;
    case SEC_NET:
        SetStatus(g_netStarted ? COL_OK : COL_FAIL,
                  g_netStarted ? "XNet up -- querying address / link" : "XNetStartup failed");
        DbgPrint("[DASH] net: started=%d\n", (int)g_netStarted);
        break;
    }
}

static void SectionExit(int s) { if (s == SEC_AUDIO) { ToneStop(); DbgPrint("[DASH] audio: tone stopped\n"); } }

static void Goto(int s) { SectionExit(g_section); g_section = s; SectionEnter(s); }

/* fullscreen effect quad (reuses the text VS: POSITION4 / TEX2 / COLOR) */
static void DrawFx(int fx, float t)
{
    g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_dev->SetVertexDeclaration(g_textDecl);
    g_dev->SetVertexShader(g_textVsh);
    g_dev->SetPixelShader(g_fx[fx]);
    float c[4] = { t, 0, 0, 0 }; g_dev->SetPixelShaderConstantF(0, c, 1);
    TEXTVERTEX q[6] = {
        { -1,  1, 0, 1, 0, 0, COL_WHITE }, {  1,  1, 0, 1, 1, 0, COL_WHITE }, { -1, -1, 0, 1, 0, 1, COL_WHITE },
        {  1,  1, 0, 1, 1, 0, COL_WHITE }, {  1, -1, 0, 1, 1, 1, COL_WHITE }, { -1, -1, 0, 1, 0, 1, COL_WHITE },
    };
    g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q, sizeof(TEXTVERTEX));
}

static void DrawTriangle(float angle)
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

/* per-section body (called after the background is drawn) */
static void SectionBody(int s, float tsec, float tglob)
{
    char line[160];
    switch (s) {
    case SEC_D3D9:
        /* triangle drawn as the background already */
        DrawText(64, 180, 1.25f, COL_WHITE, "Direct3DCreate9 + CreateDevice(HAL)");
        DrawText(64, 214, 1.25f, COL_WHITE, "hardware vertex processing, D3DX matrix math");
        DrawText(64, 248, 1.25f, COL_DIM,  "gouraud triangle via precompiled vs_3_0/ps_3_0");
        break;
    case SEC_SHADERS: {
        int fx = ((int)(tsec / 1.6f)) % 3;   /* ~1.6s per effect */
        DrawText(64, 180, 1.25f, COL_WHITE, "fullscreen quad, precompiled ps_3_0 effect:");
        for (int i = 0; i < 3; ++i) {
            wsprintfA(line, "  %s %s", (i == fx ? ">" : " "), g_fxName[i]);
            DrawText(80, 220.f + i * 30, 1.25f, i == fx ? COL_OK : COL_DIM, line);
        }
        break;
    }
    case SEC_TEXT: {
        DrawText(64, 180, 1.5f,  COL_WHITE, "The quick brown fox 0123456789");
        DrawText(64, 230, 1.0f,  COL_DIM,  "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");
        DrawText(64, 300, 2.0f,  COL_OK,   "A8 BITMAP FONT");
        DrawText(64, 360, 1.0f,  COL_WHITE,"scales 1.0 / 1.25 / 1.5 / 2.0, half-texel inset");
        break;
    }
    case SEC_AUDIO: {
        DrawText(64, 200, 1.5f, g_audioReady ? COL_OK : COL_FAIL,
                 g_audioReady ? "XAudio2 voice PLAYING" : "XAudio2 unavailable");
        DrawText(64, 250, 1.25f, COL_WHITE, "engine -> mastering voice -> source voice -> 440Hz sine");
        DrawText(64, 284, 1.25f, COL_DIM,  "tone starts on enter, stops on exit (per-section)");
        break;
    }
    case SEC_INPUT: {
        XINPUT_STATE st; ZeroMemory(&st, sizeof(st));
        DWORD r = XInputGetState(0, &st);
        if (r == ERROR_SUCCESS) {
            WORD b = st.Gamepad.wButtons;
            SetStatus(COL_OK, "controller 0 connected");
            DrawText(64, 200, 1.5f, COL_OK, "controller 0 CONNECTED");
            wsprintfA(line, "buttons 0x%04X   LT %3d RT %3d", b, st.Gamepad.bLeftTrigger, st.Gamepad.bRightTrigger);
            DrawText(64, 250, 1.25f, COL_WHITE, line);
            wsprintfA(line, "LX %6d LY %6d   RX %6d RY %6d",
                      st.Gamepad.sThumbLX, st.Gamepad.sThumbLY, st.Gamepad.sThumbRX, st.Gamepad.sThumbRY);
            DrawText(64, 284, 1.25f, COL_WHITE, line);
            const char *names[] = { "A", "B", "X", "Y", "LB", "RB", "START", "BACK" };
            WORD masks[] = { XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
                             XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
                             XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK };
            float x = 64;
            for (int i = 0; i < 8; ++i) x = DrawText(x, 330, 1.25f, (b & masks[i]) ? COL_OK : COL_DIM, names[i]) + 16;
        } else {
            SetStatus(COL_PEND, "no controller on port 0");
            DrawText(64, 200, 1.5f, COL_PEND, "no controller connected");
            DrawText(64, 250, 1.25f, COL_DIM, "XInputGetState(0) -> ERROR_DEVICE_NOT_CONNECTED");
            DrawText(64, 284, 1.25f, COL_DIM, "plug a pad (or xenia keyboard) to see live state");
        }
        break;
    }
    case SEC_NET: {
        if (!g_netStarted) {
            DrawText(64, 200, 1.5f, COL_FAIL, "XNetStartup failed");
            break;
        }
        XNADDR xna; ZeroMemory(&xna, sizeof(xna));
        DWORD f = XNetGetTitleXnAddr(&xna);
        DWORD link = XNetGetEthernetLinkStatus();
        const unsigned char *ip = (const unsigned char *)&xna.ina;
        const unsigned char *mac = xna.abEnet;

        DrawText(64, 180, 1.5f, COL_OK, "XNet initialised (xnet.lib)");

        /* link */
        if (link & XNET_ETHERNET_LINK_ACTIVE) {
            wsprintfA(line, "link: ACTIVE  %s  %s%s",
                      (link & XNET_ETHERNET_LINK_100MBPS) ? "100Mbps" :
                      (link & XNET_ETHERNET_LINK_10MBPS)  ? "10Mbps"  : "?",
                      (link & XNET_ETHERNET_LINK_FULL_DUPLEX) ? "full-duplex" :
                      (link & XNET_ETHERNET_LINK_HALF_DUPLEX) ? "half-duplex" : "",
                      (link & XNET_ETHERNET_LINK_WIRELESS) ? " wireless" : "");
        } else {
            wsprintfA(line, "link: down (0x%02X)", link);
        }
        DrawText(64, 232, 1.25f, (link & XNET_ETHERNET_LINK_ACTIVE) ? COL_OK : COL_PEND, line);

        /* address */
        if (f == XNET_GET_XNADDR_PENDING) {
            DrawText(64, 266, 1.25f, COL_PEND, "address: acquisition PENDING...");
        } else if (f & XNET_GET_XNADDR_NONE) {
            DrawText(64, 266, 1.25f, COL_PEND, "address: none (XNet uninitialised / no link)");
        } else {
            wsprintfA(line, "IP: %d.%d.%d.%d   (%s)", ip[0], ip[1], ip[2], ip[3],
                      (f & XNET_GET_XNADDR_DHCP)   ? "DHCP"   :
                      (f & XNET_GET_XNADDR_STATIC) ? "static" :
                      (f & XNET_GET_XNADDR_ETHERNET) ? "ethernet" : "?");
            DrawText(64, 266, 1.25f, COL_OK, line);
        }
        wsprintfA(line, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        DrawText(64, 300, 1.25f, COL_WHITE, line);

        /* flag summary */
        wsprintfA(line, "flags: %s%s%s%s0x%08X",
                  (f & XNET_GET_XNADDR_DNS)    ? "DNS "    : "",
                  (f & XNET_GET_XNADDR_GATEWAY)? "GATEWAY ": "",
                  (f & XNET_GET_XNADDR_ONLINE) ? "ONLINE " : "",
                  (f & XNET_GET_XNADDR_TROUBLESHOOT) ? "TROUBLESHOOT " : "", f);
        DrawText(64, 334, 1.0f, COL_DIM, line);
        break;
    }
    }
}

/* ============================ HUD ==================================== */

static void DrawHUD()
{
    char line[160];
    DrawText(48, 34, 1.6f, COL_WHITE, "RXDK-360 COMPONENT TEST HARNESS");
    DrawText(48, 78, 1.0f, COL_BLUE,  "our clang + ld.lld  |  stock XDK headers/libs  |  precompiled shaders  |  xenia");
    wsprintfA(line, "[ %d / %d ]  %s", g_section + 1, SEC_COUNT, g_secName[g_section]);
    DrawText(48, 118, 1.4f, COL_OK, line);

    /* progress ticks */
    float x = 900;
    for (int i = 0; i < SEC_COUNT; ++i) x = DrawText(x, 122, 1.4f, i == g_section ? COL_OK : COL_DIM, "#") + 6;

    /* footer: status + controls */
    DrawText(48, (float)g_bbH - 96, 1.25f, g_statusCol, g_status);
    DrawText(48, (float)g_bbH - 58, 1.0f, COL_DIM,
             g_paused ? "PAUSED   A/RB = next   LB = prev   X = resume"
                      : "auto-cycling   A/RB = next   LB = prev   X = pause");

    /* auto-advance countdown (small, right-aligned): next section in N seconds */
    if (!g_paused) {
        int secsLeft = (AUTO_FRAMES - g_frameInSec + 59) / 60;   /* ceil */
        if (secsLeft < 1) secsLeft = 1;
        if (secsLeft <= 4) {
            wsprintfA(line, "next in %d", secsLeft);
            DrawText((float)g_bbW - 260, (float)g_bbH - 58, 1.0f, COL_BLUE, line);
        }
    }
}

/* ============================ input nav ============================== */

static WORD g_prevBtn;
static void HandleNav()
{
    XINPUT_STATE st; ZeroMemory(&st, sizeof(st));
    WORD b = (XInputGetState(0, &st) == ERROR_SUCCESS) ? st.Gamepad.wButtons : 0;
    WORD pressed = b & ~g_prevBtn;
    if (pressed & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_RIGHT_SHOULDER)) Goto((g_section + 1) % SEC_COUNT);
    if (pressed & XINPUT_GAMEPAD_LEFT_SHOULDER)                      Goto((g_section + SEC_COUNT - 1) % SEC_COUNT);
    if (pressed & XINPUT_GAMEPAD_X)                                  g_paused = !g_paused;
    g_prevBtn = b;
}

/* ============================ frame ================================== */

static void DrawBackground(int s, float tsec, float tglob)
{
    if (s == SEC_D3D9)         DrawTriangle(tglob * 0.9f);
    else if (s == SEC_SHADERS) DrawFx(((int)(tsec / 1.6f)) % 3, tglob);
    /* other sections: plain cleared background */
}

static void Present() { g_dev->Present(NULL, NULL, NULL, NULL); }

static void DrawLoading(int dots)
{
    g_dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(6, 8, 22), 1.0f, 0);
    TextReset();
    DrawText(CenterX("RXDK-360", 3.0f), g_bbH * 0.40f, 3.0f, COL_WHITE, "RXDK-360");
    char l[64]; int i = 0; l[i++] = 'l'; l[i++]='o';l[i++]='a';l[i++]='d';l[i++]='i';l[i++]='n';l[i++]='g';
    for (int d = 0; d < (dots % 4); ++d) l[i++] = '.';
    l[i] = 0;
    DrawText(CenterX("loading...", 1.5f), g_bbH * 0.40f + 90, 1.5f, COL_BLUE, l);
    TextFlush();
    Present();
}

int main(void)
{
    DbgPrint("[DASH] start\n");
    if (FAILED(InitD3D())) { DbgPrint("[DASH] InitD3D FAILED\n"); return 1; }

    /* Earliest possible pixels: a bare clear+present is the smallest GPU path,
     * so it warms (JITs) and paints sooner than anything with a draw in it --
     * the screen goes from black to a colour as fast as xenia allows. Only then
     * do we bring up the text pipeline for the animated loading frames, and we
     * spread those across the heavy init so they animate rather than freeze. */
    for (int i = 0; i < 2; ++i) {
        g_dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(6, 8, 22), 1.0f, 0);
        Present();
    }
    if (FAILED(InitText())){ DbgPrint("[DASH] InitText FAILED\n"); return 1; }
    DrawLoading(0);
    InitTriangle();     DrawLoading(1);
    InitFx();           DrawLoading(2);
    InitAudioEngine();  DrawLoading(3);
    InitNet();          DrawLoading(0);
    DbgPrint("[DASH] init complete; running sections\n");

    SectionEnter(g_section);

    int frames = 0x7fffffff;
#ifndef DASH_SPIN_FOREVER
    frames = SEC_COUNT * AUTO_FRAMES + 30;   /* headless: one full cycle then DONE */
#endif
    for (int f = 0; f < frames; ++f) {
        HandleNav();
        float tsec = g_frameInSec / 60.0f, tglob = g_totalFrames / 60.0f;

        g_dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                     D3DCOLOR_XRGB(8, 10, 26), 1.0f, 0);
        DrawBackground(g_section, tsec, tglob);
        TextReset();
        SectionBody(g_section, tsec, tglob);
        DrawHUD();
        TextFlush();
        Present();

        g_frameInSec++; g_totalFrames++;
        if (!g_paused && g_frameInSec >= AUTO_FRAMES) {
            int next = (g_section + 1) % SEC_COUNT;
            if (next == 0) {
                g_cycles++;
#ifndef DASH_SPIN_FOREVER
                Goto(next); DbgPrint("[DASH] completed one full cycle\n"); break;
#endif
            }
            Goto(next);
        }
    }
    SectionExit(g_section);
    DbgPrint("[DASH] DONE\n");
    return 0;
}
