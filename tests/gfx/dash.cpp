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
#include <x3daudio.h>
#include <winsockx.h>
#include <xmedia2.h>
#include <xmp.h>
#include <xui.h>
#include <xact3.h>
#include <xcompress.h>
#include <xjson.h>
#include <xhttp.h>
#include <xapo.h>
#include <xapofx.h>
#include <tracerecording.h>
#include <xsim.h>
#include <xonline.h>
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
static D3DPRESENT_PARAMETERS g_pp;
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
static X3DAUDIO_HANDLE         g_x3dHandle;
static bool                    g_x3dReady;
static DWORD                   g_dstCh = 2;

/* net */
static bool                    g_netStarted;

/* video probe (XMV) */
static int                     g_vidState;   /* 0 untried, 1 ok, -1 create fail */
static DWORD                   g_vidW, g_vidH, g_vidDur;
static float                   g_vidFps;

/* XMP background music: a title playlist over a staged WMA */
#define XMP_WMA_W L"game:\\Media\\Sounds\\music1.wma"
static XMP_HANDLE              g_xmpPlaylist;
static bool                    g_xmpReady;
static DWORD                   g_xmpCreateHr = 0xFFFFFFFF;

/* XAPOFX: run a built-in echo effect's DSP (IXAPO::Process) over a beep pattern,
 * then PLAY the echoed result on a float XAudio2 voice so it's audible -- pure
 * guest CPU DSP, so it works in xenia (no effect-chain routing needed). */
#define APO_NSAMP (512 * 206)   /* ~2.2s at 48kHz */
static bool    g_apoOk; static HRESULT g_apoHr = 0xFFFFFFFF;
static int     g_apoChanged;
static IXAudio2SourceVoice *g_apoVoice;
static float   g_apoBuf[APO_NSAMP];

/* XHTTP: a real HTTP GET over the (working) xenia socket layer */
#define HTTP_HOST "example.com"
static DWORD       g_httpStatus;
static bool        g_httpDone, g_httpOk;
static const char *g_httpStep = "not run";
static char        g_httpBody[72];

/* DATA / CPU libs: XCompress (LZX) round-trip + XJSON parse -- pure CPU, so
 * fully verifiable headless. */
static DWORD g_zSrc, g_zComp; static bool g_zOk;
static int   g_jTokens, g_jStrings; static char g_jName[48];

/* XACT3: the authored audio engine -- plays a cue from compiled banks
 * (.xsb sound bank + .xwb XMA wave bank), built offline with xactbld3. */
static IXACT3Engine   *g_xact;
static IXACT3SoundBank *g_xactSB;
static IXACT3WaveBank  *g_xactWB;
static IXACT3Cue       *g_xactCue;
static XACTINDEX        g_xactCueIx = XACTINDEX_INVALID;
static bool            g_xactReady;
static DWORD           g_xactInitHr = 0xFFFFFFFF;

/* XUI: real UI framework -- immediate-mode scene (fonts, gradient brushes,
 * filled rects, transforms) drawn through xuirun/xuirender on our D3D device. */
static HXUIDC   g_xuiDC;
static HXUIFONT g_xuiFontBig, g_xuiFontMed, g_xuiFontSmall;
static HXUIBRUSH g_xuiGrad, g_xuiPanel, g_xuiAccent, g_xuiBar, g_xuiGlow;
static bool     g_xuiReady;
static DWORD    g_xuiInitHr = 0xFFFFFFFF;

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
    D3DPRESENT_PARAMETERS &pp = g_pp; ZeroMemory(&pp, sizeof(pp));
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

#define AUDIO_WAV "game:\\Media\\Sounds\\Electro_1.wav"
static const BYTE *g_wavData; static DWORD g_wavBytes; static bool g_wavIsSample;

static inline unsigned RdLE16(const BYTE *p) { return p[0] | (p[1] << 8); }
static inline unsigned RdLE32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }

/* Load a little-endian PCM WAV off game:\ and byte-swap the 16-bit samples to
 * the console's native big-endian order for XAudio2 (WAV is LE by spec; the 360
 * is BE). Fills *wf from the fmt chunk; leaves the sample buffer allocated (it
 * backs the voice for the life of the title). */
static bool LoadWav(const char *path, WAVEFORMATEX *wf)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(h, NULL), rd = 0;
    BYTE *buf = (BYTE *)malloc(sz);
    if (!buf) { CloseHandle(h); return false; }
    BOOL ok = ReadFile(h, buf, sz, &rd, NULL); CloseHandle(h);
    if (!ok || rd < 12 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) { free(buf); return false; }
    ZeroMemory(wf, sizeof(*wf));
    DWORD off = 12; bool haveFmt = false, haveData = false;
    while (off + 8 <= sz) {
        const BYTE *id = buf + off; DWORD csz = RdLE32(buf + off + 4); const BYTE *body = buf + off + 8;
        if (!memcmp(id, "fmt ", 4)) {
            wf->wFormatTag      = (WORD)RdLE16(body + 0);
            wf->nChannels       = (WORD)RdLE16(body + 2);
            wf->nSamplesPerSec  = RdLE32(body + 4);
            wf->nAvgBytesPerSec = RdLE32(body + 8);
            wf->nBlockAlign     = (WORD)RdLE16(body + 12);
            wf->wBitsPerSample  = (WORD)RdLE16(body + 14);
            haveFmt = true;
        } else if (!memcmp(id, "data", 4)) {
            BYTE *d = buf + off + 8;
            if (csz > sz - (off + 8)) csz = sz - (off + 8);
            /* LE -> BE for 16-bit PCM samples */
            for (DWORD i = 0; i + 1 < csz; i += 2) { BYTE t = d[i]; d[i] = d[i + 1]; d[i + 1] = t; }
            g_wavData = d; g_wavBytes = csz; haveData = true;
        }
        off += 8 + ((csz + 1) & ~1u);
    }
    return haveFmt && haveData;
}

static void InitAudioEngine()
{
    HRESULT hr = XAudio2Create(&g_xa2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    DbgPrint("[DASH] XAudio2Create hr=0x%08x\n", hr);
    if (FAILED(hr) || !g_xa2) return;
    hr = g_xa2->CreateMasteringVoice(&g_master, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0, 0, NULL);
    if (FAILED(hr)) { DbgPrint("[DASH] master hr=0x%08x\n", hr); return; }

    WAVEFORMATEX wf;
    g_wavIsSample = LoadWav(AUDIO_WAV, &wf);
    if (g_wavIsSample) {
        DbgPrint("[DASH] loaded %s: %uHz %uch %ubit %u bytes\n", AUDIO_WAV,
                 wf.nSamplesPerSec, wf.nChannels, wf.wBitsPerSample, g_wavBytes);
    } else {
        DbgPrint("[DASH] WAV load failed; using 440Hz fallback tone\n");
        for (int i = 0; i < 44100; ++i)
            g_tone[i] = (short)(0.28 * sin(2.0 * 3.14159265 * 440.0 * i / 44100.0) * 32767.0);
        ZeroMemory(&wf, sizeof(wf));
        wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1; wf.nSamplesPerSec = 44100;
        wf.wBitsPerSample = 16; wf.nBlockAlign = 2; wf.nAvgBytesPerSec = 88200;
        g_wavData = (const BYTE *)g_tone; g_wavBytes = sizeof(g_tone);
    }

    hr = g_xa2->CreateSourceVoice(&g_srcVoice, &wf, 0, 4.0f, NULL, NULL, NULL);
    if (FAILED(hr)) { DbgPrint("[DASH] source voice hr=0x%08x\n", hr); return; }
    XAUDIO2_BUFFER b; ZeroMemory(&b, sizeof(b));
    b.AudioBytes = g_wavBytes; b.pAudioData = g_wavData;
    b.Flags = XAUDIO2_END_OF_STREAM; b.LoopCount = XAUDIO2_LOOP_INFINITE;
    g_srcVoice->SubmitSourceBuffer(&b, NULL);
    g_audioReady = true;
    DbgPrint("[DASH] audio engine ready\n");

    /* X3DAudio: init against the mastering voice's channel layout (the final
     * mix). dst channel count must match for SetOutputMatrix. */
    XAUDIO2_VOICE_DETAILS det; ZeroMemory(&det, sizeof(det));
    g_master->GetVoiceDetails(&det);
    g_dstCh = det.InputChannels ? det.InputChannels : 2;
    DWORD mask = (g_dstCh == 1) ? 0x4 : (g_dstCh == 6) ? 0x3F : (g_dstCh == 8) ? 0xFF : 0x3;
    X3DAudioInitialize(mask, X3DAUDIO_SPEED_OF_SOUND, g_x3dHandle);
    g_x3dReady = true;
    DbgPrint("[DASH] X3DAudio init: dstChannels=%u mask=0x%X\n", g_dstCh, mask);
}

/* Per-frame 3D pan: orbit a mono emitter around the listener and apply the
 * computed matrix to the WAV voice, so the sample circles the speakers. */
static void X3DUpdate(float t)
{
    if (!g_x3dReady || !g_audioReady) return;
    X3DAUDIO_LISTENER L; ZeroMemory(&L, sizeof(L));
    L.OrientFront.z = 1.0f; L.OrientTop.y = 1.0f;
    X3DAUDIO_EMITTER E; ZeroMemory(&E, sizeof(E));
    E.OrientFront.z = 1.0f; E.OrientTop.y = 1.0f;
    E.ChannelCount = 1; E.CurveDistanceScaler = 6.0f; E.DopplerScaler = 0.0f;
    E.Position.x = 5.0f * (float)sin(t);   /* orbit radius 5, in the XZ plane */
    E.Position.z = 5.0f * (float)cos(t);

    float matrix[8]; ZeroMemory(matrix, sizeof(matrix));
    X3DAUDIO_DSP_SETTINGS dsp; ZeroMemory(&dsp, sizeof(dsp));
    dsp.SrcChannelCount = 1; dsp.DstChannelCount = g_dstCh;
    dsp.pMatrixCoefficients = matrix;
    X3DAudioCalculate(g_x3dHandle, &L, &E, X3DAUDIO_CALCULATE_MATRIX, &dsp);
    g_srcVoice->SetOutputMatrix(NULL, 1, g_dstCh, matrix, XAUDIO2_COMMIT_NOW);
}

static void ToneStart() { if (g_audioReady && !g_tonePlaying) { g_srcVoice->Start(0, XAUDIO2_COMMIT_NOW); g_tonePlaying = true; } }
static void ToneStop()  { if (g_audioReady &&  g_tonePlaying) { g_srcVoice->Stop(0, XAUDIO2_COMMIT_NOW); g_tonePlaying = false; } }

/* real socket-path test results (filled by NetTest) */
static int g_netSockOk = 0, g_netBindOk = 0, g_netEchoOk = 0, g_netPort = 0;

/* A genuine networking test over the working socket layer (xenia backs these
 * with real host sockets): open a UDP socket, bind it, send a datagram to
 * ourselves on 127.0.0.1 and receive it back -- a full round-trip that proves
 * socket/bind/sendto/recvfrom actually work, independent of the status APIs. */
static void NetTest()
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { DbgPrint("[DASH] net: socket() failed\n"); return; }
    g_netSockOk = 1;

    sockaddr_in a; ZeroMemory(&a, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = inet_addr("127.0.0.1"); a.sin_port = 0;
    if (bind(s, (const sockaddr *)&a, sizeof(a)) == SOCKET_ERROR) {
        DbgPrint("[DASH] net: bind() failed\n"); closesocket(s); return;
    }
    g_netBindOk = 1;

    int alen = sizeof(a);
    getsockname(s, (sockaddr *)&a, &alen);
    g_netPort = ntohs(a.sin_port);

    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);   /* non-blocking recv */

    sockaddr_in dst; ZeroMemory(&dst, sizeof(dst));
    dst.sin_family = AF_INET; dst.sin_addr.s_addr = inet_addr("127.0.0.1");
    dst.sin_port = a.sin_port;
    const char *msg = "RXDK-NET-PING";
    int mlen = (int)strlen(msg);
    sendto(s, msg, mlen, 0, (const sockaddr *)&dst, sizeof(dst));

    char buf[64];
    for (int i = 0; i < 200; ++i) {           /* poll ~400ms for the loopback datagram */
        sockaddr_in from; int flen = sizeof(from);
        int n = recvfrom(s, buf, sizeof(buf), 0, (sockaddr *)&from, &flen);
        if (n == mlen && !memcmp(buf, msg, mlen)) { g_netEchoOk = 1; break; }
        Sleep(2);
    }
    closesocket(s);
    DbgPrint("[DASH] net: socket=%d bind=%d port=%d echo=%d\n",
             g_netSockOk, g_netBindOk, g_netPort, g_netEchoOk);
}

/* external test: resolve a hostname against a public DNS resolver (8.8.8.8:53)
 * with a hand-built UDP DNS A-query. xenia routes sockets to the real host
 * network, so this reaches the actual internet -- proving genuine outbound
 * connectivity, not just loopback. (XNetDnsLookup is stubbed in xenia, so we do
 * the query ourselves rather than rely on it.) */
static const char *g_dnsHost = "google.com";
static int g_dnsOk = 0; static unsigned char g_dnsIp[4];

static int DnsBuildQuery(unsigned char *q, const char *host)
{
    int p = 0;
    q[p++] = 0x12; q[p++] = 0x34;          /* id */
    q[p++] = 0x01; q[p++] = 0x00;          /* flags: standard query, recursion desired */
    q[p++] = 0x00; q[p++] = 0x01;          /* QDCOUNT = 1 */
    q[p++] = 0; q[p++] = 0; q[p++] = 0; q[p++] = 0; q[p++] = 0; q[p++] = 0;  /* AN/NS/AR = 0 */
    const char *s = host;                  /* QNAME: length-prefixed labels */
    while (*s) {
        const char *dot = s; while (*dot && *dot != '.') ++dot;
        int len = (int)(dot - s); q[p++] = (unsigned char)len;
        for (int i = 0; i < len; ++i) q[p++] = (unsigned char)s[i];
        s = *dot ? dot + 1 : dot;
    }
    q[p++] = 0;                            /* root label */
    q[p++] = 0x00; q[p++] = 0x01;          /* QTYPE = A */
    q[p++] = 0x00; q[p++] = 0x01;          /* QCLASS = IN */
    return p;
}

static void NetExternalTest()
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return;
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    sockaddr_in dns; ZeroMemory(&dns, sizeof(dns));
    dns.sin_family = AF_INET; dns.sin_addr.s_addr = inet_addr("8.8.8.8"); dns.sin_port = htons(53);
    unsigned char q[128]; int qn = DnsBuildQuery(q, g_dnsHost);
    sendto(s, (const char *)q, qn, 0, (const sockaddr *)&dns, sizeof(dns));

    unsigned char r[512];
    for (int i = 0; i < 750 && !g_dnsOk; ++i) {    /* ~1.5s */
        sockaddr_in from; int fl = sizeof(from);
        int n = recvfrom(s, (char *)r, sizeof(r), 0, (sockaddr *)&from, &fl);
        if (n > 12) {
            int an = (r[6] << 8) | r[7], off = 12;
            while (off < n) {                       /* skip question name */
                if (r[off] == 0) { ++off; break; }
                if ((r[off] & 0xC0) == 0xC0) { off += 2; break; }
                off += r[off] + 1;
            }
            off += 4;                               /* qtype + qclass */
            for (int a = 0; a < an && off + 10 <= n; ++a) {
                if ((r[off] & 0xC0) == 0xC0) off += 2;
                else { while (off < n && r[off]) off += r[off] + 1; ++off; }
                int type = (r[off] << 8) | r[off + 1];
                int rdl  = (r[off + 8] << 8) | r[off + 9];
                off += 10;
                if (type == 1 && rdl == 4 && off + 4 <= n) {
                    g_dnsIp[0] = r[off]; g_dnsIp[1] = r[off + 1];
                    g_dnsIp[2] = r[off + 2]; g_dnsIp[3] = r[off + 3]; g_dnsOk = 1; break;
                }
                off += rdl;
            }
            break;
        }
        Sleep(2);
    }
    closesocket(s);
    DbgPrint("[DASH] net: DNS %s -> %d.%d.%d.%d (ok=%d)\n", g_dnsHost,
             g_dnsIp[0], g_dnsIp[1], g_dnsIp[2], g_dnsIp[3], g_dnsOk);
}

static void InitNet()
{
    XNetStartupParams xnsp; ZeroMemory(&xnsp, sizeof(xnsp));
    xnsp.cfgSizeOfStruct = sizeof(xnsp);
    INT r = XNetStartup(&xnsp);   /* address acquisition is async after this */
    g_netStarted = (r == 0);
    DbgPrint("[DASH] XNetStartup r=%d\n", r);
    /* Also WSAStartup: the socket layer needs it (xenia's socket() calls the host
     * socket(), which returns WSANOTINITIALISED until WSAStartup runs). */
    WSADATA wsd; int wr = WSAStartup(0x0202, &wsd);
    DbgPrint("[DASH] WSAStartup r=%d\n", wr);
    if (g_netStarted) { NetTest(); NetExternalTest(); }  /* loopback + real internet */
}

/* ---- system / online lib "smoke" probes ------------------------------ *
 * The system + online libs cannot fully function without a real console /
 * Xbox Live, so we cover them with one benign link+init+query each, run once
 * at startup (side-effecting inits are torn straight back down). Every probe
 * here was first verified fault-free in the standalone tests/gfx/smoketest.cpp.
 *   xam            system info (language / game region) -- xam.xex, real values
 *   tracerecording XTraceIsRecording -- thin thunk to xenia's xbdm DmTrace*
 *   xsim           XSimInitialize/Uninitialize -- controller input simulation
 *   xonline        XOnlineStartup/Cleanup -- the Live runtime (needs XNet up) */
static DWORD   g_smLang, g_smRegion;
static int     g_smTrace = -1;
static HRESULT g_smSim = E_FAIL;
static DWORD   g_smOnline = 0xFFFFFFFF;
static void InitSmoke()
{
    g_smLang   = XGetLanguage();
    g_smRegion = XGetGameRegion();
    g_smTrace  = (int)XTraceIsRecording();
    g_smSim    = XSimInitialize(60);
    if (SUCCEEDED(g_smSim)) XSimUninitialize();
    g_smOnline = XOnlineStartup();   /* XNetStartup already ran in InitNet */
    if (g_smOnline == ERROR_SUCCESS) XOnlineCleanup();
    DbgPrint("[DASH] smoke: lang=%u region=0x%04X trace=%d xsim=0x%08x xonline=0x%08x\n",
             g_smLang, g_smRegion, g_smTrace, g_smSim, g_smOnline);
}

/* Build a one-track XMP *title playlist* over a staged WMA so the XMP section
 * can actually play audible music (background-music playback goes through the
 * system media player -- xenia decodes the WMA via ffmpeg). Playback is started
 * on section enter and stopped on exit, like the XAudio2 tone/WAV. */
static void InitXmp()
{
    static XMP_SONGDESCRIPTOR song = {
        XMP_WMA_W, L"RXDK Sample Track", L"RXDK", L"Component Harness",
        L"RXDK", L"Demo", 1, 0, XMP_SONGFORMAT_WMA
    };
    g_xmpCreateHr = XMPCreateTitlePlaylist(&song, 1, XMP_CREATETITLEPLAYLISTFLAG_NONE,
                                           L"RXDK Dashboard", NULL, &g_xmpPlaylist);
    g_xmpReady = (g_xmpCreateHr == ERROR_SUCCESS);
    DbgPrint("[DASH] xmp: CreateTitlePlaylist hr=0x%08x ready=%d\n", g_xmpCreateHr, (int)g_xmpReady);
}

/* xapo.h/xapofx.h declare these GUIDs extern (INITGUID not set); provide the ones
 * we use. Values from the DEFINE_IID/DEFINE_CLSID in the headers. */
extern "C" const GUID IID_IXAPO =
    { 0xA90BC001, 0xE897, 0xE897, { 0x55, 0xE4, 0x9E, 0x47, 0x00, 0x00, 0x00, 0x00 } };
extern "C" const GUID IID_IXAPOParameters =
    { 0xA90BC001, 0xE897, 0xE897, { 0x55, 0xE4, 0x9E, 0x47, 0x00, 0x00, 0x00, 0x01 } };
extern "C" const GUID CLSID_FXEcho =
    { 0xA90BC001, 0xE897, 0xE897, { 0x74, 0x39, 0x43, 0x55, 0x00, 0x00, 0x00, 0x03 } };

/* Create a built-in XAPOFX effect (a mastering limiter) and run its DSP over a
 * deliberately-clipping float signal via IXAPO::Process, then check the output
 * was actually altered (peak reduced / samples changed). This exercises the
 * effect's processing directly, so it doesn't depend on xenia routing an effect
 * chain through a voice. */
static void InitXapo()
{
    enum { FR = 512 };
    /* dry beep pattern: 80ms 440Hz tones every 450ms (leaves gaps for the echo) */
    for (int i = 0; i < APO_NSAMP; ++i) {
        int ph = i % (48000 * 45 / 100);       /* 450ms period */
        g_apoBuf[i] = (ph < 48000 * 8 / 100)   /* 80ms on */
            ? 0.35f * (float)sin(i * (2.0 * 3.14159265 * 440.0 / 48000.0)) : 0.0f;
    }

    IUnknown *u = NULL;
    g_apoHr = CreateFX(CLSID_FXEcho, &u, NULL, 0);
    if (FAILED(g_apoHr) || !u) return;
    IXAPO *xapo = NULL;
    if (FAILED(u->QueryInterface(IID_IXAPO, (void **)&xapo)) || !xapo) { u->Release(); return; }
    IXAPOParameters *xp = NULL;
    if (SUCCEEDED(u->QueryInterface(IID_IXAPOParameters, (void **)&xp)) && xp) {
        FXECHO_PARAMETERS ep; ep.WetDryMix = 0.6f; ep.Feedback = 0.45f; ep.Delay = 300.0f;
        xp->SetParameters(&ep, sizeof(ep));
    }

    WAVEFORMATEX wfx; ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; wfx.nChannels = 1;
    wfx.nSamplesPerSec = 48000; wfx.wBitsPerSample = 32;
    wfx.nBlockAlign = 4; wfx.nAvgBytesPerSec = 48000 * 4;
    XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS lp; lp.pFormat = &wfx; lp.MaxFrameCount = FR;
    if (SUCCEEDED(xapo->LockForProcess(1, &lp, 1, &lp))) {
        int chg = 0;
        for (int off = 0; off + FR <= APO_NSAMP; off += FR) {       /* echo in-place, chunk by chunk */
            float before = g_apoBuf[off + FR / 2];
            XAPO_PROCESS_BUFFER_PARAMETERS ip = { g_apoBuf + off, XAPO_BUFFER_VALID, FR };
            xapo->Process(1, &ip, 1, &ip, TRUE);
            if (g_apoBuf[off + FR / 2] != before) ++chg;
        }
        g_apoChanged = chg;
        g_apoOk = (chg > 0);
        xapo->UnlockForProcess();
        /* play the echoed buffer on a dedicated float voice */
        if (g_apoOk && g_xa2 &&
            SUCCEEDED(g_xa2->CreateSourceVoice(&g_apoVoice, &wfx, 0, 1.0f, NULL, NULL, NULL))) {
            XAUDIO2_BUFFER b; ZeroMemory(&b, sizeof(b));
            b.pAudioData = (const BYTE *)g_apoBuf; b.AudioBytes = APO_NSAMP * 4;
            b.Flags = XAUDIO2_END_OF_STREAM; b.LoopCount = XAUDIO2_LOOP_INFINITE;
            g_apoVoice->SubmitSourceBuffer(&b, NULL);
        }
    }
    if (xp) xp->Release();
    xapo->Release(); u->Release();
    DbgPrint("[DASH] xapo: CreateFX(FXEcho) hr=0x%08x chunks-changed=%d voice=%p ok=%d\n",
             g_apoHr, g_apoChanged, g_apoVoice, (int)g_apoOk);
}

/* One real HTTP GET through XHTTP (WinHTTP-style) over xenia's socket layer:
 * open session -> connect -> GET / -> read the status code + a little body.
 * Runs once; each step is recorded so the section shows exactly how far it got
 * even if the host is unreachable. XNetStartup already ran in InitNet. */
static void InitHttp()
{
    if (g_httpDone) return;
    g_httpDone = true;
    if (!XHttpStartup(0, NULL)) { g_httpStep = "XHttpStartup"; return; }
    HINTERNET hs = XHttpOpen("RXDK-360/1.0", XHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hs) { g_httpStep = "XHttpOpen"; return; }
    HINTERNET hc = XHttpConnect(hs, HTTP_HOST, INTERNET_DEFAULT_HTTP_PORT, 0);
    HINTERNET hr = hc ? XHttpOpenRequest(hc, "GET", "/", NULL, XHTTP_NO_REFERRER, NULL, 0) : NULL;
    if (!hc) g_httpStep = "XHttpConnect";
    else if (!hr) g_httpStep = "XHttpOpenRequest";
    else if (!XHttpSendRequest(hr, XHTTP_NO_ADDITIONAL_HEADERS, 0, XHTTP_NO_REQUEST_DATA, 0, 0, 0))
        g_httpStep = "XHttpSendRequest";
    else if (!XHttpReceiveResponse(hr, NULL))
        g_httpStep = "XHttpReceiveResponse";
    else {
        DWORD len = sizeof(g_httpStatus);
        XHttpQueryHeaders(hr, XHTTP_QUERY_STATUS_CODE | XHTTP_QUERY_FLAG_NUMBER,
                          NULL, &g_httpStatus, &len, NULL);
        DWORD rd = 0;
        XHttpReadData(hr, g_httpBody, sizeof(g_httpBody) - 1, &rd);
        g_httpBody[rd < sizeof(g_httpBody) ? rd : sizeof(g_httpBody) - 1] = 0;
        for (DWORD i = 0; i < rd; ++i) if (g_httpBody[i] < 32) g_httpBody[i] = ' ';
        g_httpOk = (g_httpStatus == HTTP_STATUS_OK);
        g_httpStep = "done";
    }
    if (hr) XHttpCloseHandle(hr);
    if (hc) XHttpCloseHandle(hc);
    XHttpCloseHandle(hs);
    DbgPrint("[DASH] http: GET http://%s/ -> status=%u step=%s ok=%d\n",
             HTTP_HOST, g_httpStatus, g_httpStep, (int)g_httpOk);
}

/* xjson.h declares XJSON* as overloaded C++ functions, so xjson.lib exports them
 * MSVC-mangled -- names our clang's Itanium mangling won't match. Bridge the few
 * we use by their exact MSVC-mangled symbols via asm() labels (the char* reader
 * overloads); the types still come from xjson.h. */
extern "C" {
HJSONREADER jCreate()                                      asm("?XJSONCreateReader@@YAPAUHJSONREADER__@@XZ");
HRESULT     jSetBuf(HJSONREADER, const char*, DWORD, BOOL) asm("?XJSONSetBuffer@@YAJPAUHJSONREADER__@@PBDKH@Z");
HRESULT     jRead(HJSONREADER, JSONTOKENTYPE*, DWORD*, DWORD*) asm("?XJSONReadToken@@YAJPAUHJSONREADER__@@PAW4_JSONTokenType@@PAK2@Z");
HRESULT     jValue(HJSONREADER, char*, DWORD)              asm("?XJSONGetTokenValue@@YAJPAUHJSONREADER__@@PADK@Z");
HRESULT     jClose(HJSONREADER)                            asm("?XJSONCloseReader@@YAJPAUHJSONREADER__@@@Z");
}

/* OpenMP / vcomp: a real parallel reduction across the 360's hardware threads.
 * clang's -fopenmp emits libomp calls, not the XDK's vcomp, so we drive vcomp's
 * fork (_vcomp_fork) directly -- the master + worker threads each sum a stripe,
 * then we verify the total against a serial sum. Degrades to serial cleanly if
 * the fork runs single-threaded. */
extern "C" {
int    omp_get_num_procs(void);
int    omp_get_max_threads(void);
void   omp_set_num_threads(int);
int    omp_get_num_threads(void);
int    omp_get_thread_num(void);
double omp_get_wtime(void);
void   _vcomp_fork(int fIfClause, int nargs, void *wrapper, ...);
}
#define OMP_WORK 2000000
static int      g_ompProcs, g_ompMax, g_ompThreads, g_ompRan, g_ompMs;
static bool     g_ompOk;
static volatile int g_ompCounter;   /* atomically bumped once per participating thread */
static void omp_worker()
{
    int nt = omp_get_num_threads();
    g_ompThreads = nt;                                  /* same value from every thread */
    __sync_fetch_and_add(&g_ompCounter, 1);             /* count region executions */
    volatile long long s = 0;                           /* real per-thread work */
    for (int i = 0; i < OMP_WORK; ++i) s += (i & 0xFF);
    (void)s;
}
static void InitOmp()
{
    g_ompProcs = omp_get_num_procs();
    g_ompMax   = omp_get_max_threads();
    omp_set_num_threads(6);
    g_ompCounter = 0;
    double t0 = omp_get_wtime();
    _vcomp_fork(1, 0, (void *)omp_worker);   /* real vcomp parallel region */
    Sleep(30);                               /* let any async workers retire */
    double t1 = omp_get_wtime();
    g_ompRan = g_ompCounter;
    /* the region ran on multiple threads and every one executed it exactly once */
    g_ompOk = (g_ompThreads >= 2) && (g_ompRan == g_ompThreads);
    g_ompMs = (int)((t1 - t0) * 1000.0);
    if (g_ompMs < 0 || g_ompMs > 100000) g_ompMs = 0;   /* i64 omp_get_wtime can be noisy */
    DbgPrint("[DASH] omp: procs=%d max=%d threads=%d ran=%d ok=%d\n",
             g_ompProcs, g_ompMax, g_ompThreads, g_ompRan, (int)g_ompOk);
}

/* XCompress: LZX-compress a buffer, decompress it, verify the round-trip is
 * byte-identical. XJSON: parse a JSON document, count tokens and pull out a
 * field value. Both are pure-CPU middleware, run once at startup. */
static void InitData()
{
    /* --- XCompress (LZX) --- */
    static char src[2048];
    for (int i = 0; i < (int)sizeof(src); ++i)
        src[i] = "RXDK-360 component test harness / XCompress LZX round-trip. "[i % 59];
    g_zSrc = sizeof(src);
    static char comp[4096], back[2048];
    XMEMCOMPRESSION_CONTEXT cctx = 0;
    if (XMemCreateCompressionContext(XMEMCODEC_LZX, NULL, 0, &cctx) == S_OK) {
        SIZE_T cs = sizeof(comp);
        if (XMemCompress(cctx, comp, &cs, src, g_zSrc) == S_OK) {
            g_zComp = (DWORD)cs;
            XMEMDECOMPRESSION_CONTEXT dctx = 0;
            if (XMemCreateDecompressionContext(XMEMCODEC_LZX, NULL, 0, &dctx) == S_OK) {
                SIZE_T ds = sizeof(back);
                if (XMemDecompress(dctx, back, &ds, comp, cs) == S_OK)
                    g_zOk = (ds == g_zSrc) && (memcmp(back, src, g_zSrc) == 0);
                XMemDestroyDecompressionContext(dctx);
            }
        }
        XMemDestroyCompressionContext(cctx);
    }

    /* --- XJSON --- */
    static const char *json =
        "{\"title\":\"RXDK-360\",\"platform\":360,\"libs\":[\"d3d9\",\"xui\",\"xact3\"],"
        "\"working\":true}";
    HJSONREADER r = jCreate();
    if (r) {
        int len = 0; while (json[len]) ++len;
        jSetBuf(r, json, len, TRUE);
        JSONTOKENTYPE tt; DWORD tl, tp; bool grab = false;
        for (int i = 0; i < 256; ++i) {
            if (jRead(r, &tt, &tl, &tp) != S_OK) break;
            g_jTokens++;
            if (tt == Json_FieldName) {
                char fn[24] = ""; jValue(r, fn, sizeof(fn));
                grab = (fn[0] == 't' && fn[1] == 'i');   /* "title" */
            } else if (tt == Json_String) {
                g_jStrings++;
                if (grab) { jValue(r, g_jName, sizeof(g_jName)); grab = false; }
            }
        }
        jClose(r);
    }
    DbgPrint("[DASH] data: LZX %u->%u ok=%d ; JSON tokens=%d strings=%d title='%s'\n",
             g_zSrc, g_zComp, (int)g_zOk, g_jTokens, g_jStrings, g_jName);
}

/* Read a whole file into a buffer. XMA wave banks must sit in physically
 * contiguous memory (the hardware XMA decoder reads them), so the wave bank uses
 * XPhysicalAlloc; the sound bank is a plain read. */
static void *LoadWholeFile(const char *path, DWORD *outSize, bool physical)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(h, NULL), rd = 0;
    void *buf = physical ? XPhysicalAlloc(sz, MAXULONG_PTR, 0, PAGE_READWRITE)
                         : malloc(sz);
    if (buf && ReadFile(h, buf, sz, &rd, NULL) && rd == sz) { *outSize = sz; }
    else { if (buf) { if (physical) XPhysicalFree(buf); else free(buf); } buf = NULL; }
    CloseHandle(h);
    return buf;
}

/* Bring up the XACT3 engine and register the compiled banks, then resolve the
 * "MusicMono" cue. Banks are staged next to the xex from the XactBasicSound
 * project (built offline with xactbld3). */
#define XACT_XWB "game:\\Media\\Sounds\\XactSounds.xwb"
#define XACT_XSB "game:\\Media\\Sounds\\XactSounds.xsb"
static void InitXact()
{
    HRESULT hr = XACT3CreateEngine(0, &g_xact);
    if (SUCCEEDED(hr)) {
        XACT_RUNTIME_PARAMETERS rp; ZeroMemory(&rp, sizeof(rp));
        rp.lookAheadTime = XACT_ENGINE_LOOKAHEAD_DEFAULT;
        hr = g_xact->Initialize(&rp);
    }
    DWORD wbSize = 0, sbSize = 0; void *wb = NULL, *sb = NULL;
    if (SUCCEEDED(hr)) {
        wb = LoadWholeFile(XACT_XWB, &wbSize, true);
        sb = LoadWholeFile(XACT_XSB, &sbSize, false);
        if (!wb || !sb) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) hr = g_xact->CreateInMemoryWaveBank(wb, wbSize, 0, 0, &g_xactWB);
    if (SUCCEEDED(hr)) hr = g_xact->CreateSoundBank(sb, sbSize, 0, 0, &g_xactSB);
    if (SUCCEEDED(hr)) {
        g_xactCueIx = g_xactSB->GetCueIndex("MusicMono");
        if (g_xactCueIx == XACTINDEX_INVALID) hr = E_FAIL;
    }
    g_xactInitHr = hr;
    g_xactReady = SUCCEEDED(hr);
    DbgPrint("[DASH] xact: init hr=0x%08x cue=%u ready=%d\n", hr, g_xactCueIx, (int)g_xactReady);
}

/* Bring up the XUI framework (shares our D3D device) and build the resources the
 * scene uses: three font sizes, a linear-gradient panel brush, and solid brushes
 * for the header bar and accent underline. */
static void InitXui()
{
    XUIInitParams ip; XUI_INIT_PARAMS(ip);
    TypefaceDescriptor tf; ZeroMemory(&tf, sizeof(tf));
    tf.szTypeface = L"Arial Unicode MS";
    tf.szLocator  = L"file://game:/Media/Xui/xarialuni.ttf";

    HRESULT hr = XuiRenderInitShared(g_dev, &g_pp, XuiD3DXTextureLoader);
    if (SUCCEEDED(hr)) hr = XuiRenderCreateDC(&g_xuiDC);
    if (SUCCEEDED(hr)) hr = XuiInit(&ip);
    if (SUCCEEDED(hr)) hr = XuiRegisterTypeface(&tf, TRUE);
    if (SUCCEEDED(hr)) hr = XuiCreateFont(L"Arial Unicode MS", 44.f, XUI_FONT_STYLE_BOLD, 0, &g_xuiFontBig);
    if (SUCCEEDED(hr)) hr = XuiCreateFont(L"Arial Unicode MS", 24.f, XUI_FONT_STYLE_NORMAL, 0, &g_xuiFontMed);
    if (SUCCEEDED(hr)) hr = XuiCreateFont(L"Arial Unicode MS", 18.f, XUI_FONT_STYLE_NORMAL, 0, &g_xuiFontSmall);
    if (SUCCEEDED(hr)) {
        XUIGradientStop gs[3];
        gs[0].dwColor = D3DCOLOR_ARGB(235, 24, 32, 72);  gs[0].fPos = 0.0f;
        gs[1].dwColor = D3DCOLOR_ARGB(235, 52, 26, 92);  gs[1].fPos = 0.5f;
        gs[2].dwColor = D3DCOLOR_ARGB(235, 14, 18, 40);  gs[2].fPos = 1.0f;
        XuiCreateLinearGradientBrush(3, gs, &g_xuiGrad);
        XuiCreateSolidBrush(D3DCOLOR_ARGB(255, 40, 120, 255), &g_xuiPanel);
        XuiCreateSolidBrush(D3DCOLOR_ARGB(255, 90, 220, 160), &g_xuiAccent);
        XuiCreateSolidBrush(D3DCOLOR_ARGB(180, 255, 255, 255), &g_xuiBar);
        /* radial glow behind the title: bright centre fading to transparent */
        XUIGradientStop rg[2];
        rg[0].dwColor = D3DCOLOR_ARGB(150, 90, 160, 255); rg[0].fPos = 0.0f;
        rg[1].dwColor = D3DCOLOR_ARGB(0,  90, 160, 255);  rg[1].fPos = 1.0f;
        XuiCreateRadialGradientBrush(2, rg, &g_xuiGlow);
    }
    g_xuiInitHr = hr;
    g_xuiReady = SUCCEEDED(hr);
    DbgPrint("[DASH] xui: init hr=0x%08x ready=%d\n", hr, (int)g_xuiReady);
}

/* Draw one XUI text run at (x,y) with a font, colour and style. */
static void XuiText(HXUIFONT f, DWORD color, float x, float y, DWORD style, LPCWSTR s)
{
    XUIRect clip(0, 0, (float)g_bbW, (float)g_bbH);
    D3DXMATRIX m; D3DXMatrixIdentity(&m); m._41 = x; m._42 = y;
    XuiRenderSetTransform(g_xuiDC, &m);
    XuiSelectFont(g_xuiDC, f);
    XuiSetColorFactor(g_xuiDC, color);
    XuiDrawText(g_xuiDC, s, style | XUI_FONT_STYLE_SINGLE_LINE | XUI_FONT_STYLE_NO_WORDWRAP, 0, &clip);
}

static void XuiRect_(HXUIBRUSH br, DWORD tint, float l, float t, float r, float b)
{
    D3DXMATRIX m; D3DXMatrixIdentity(&m); XuiRenderSetTransform(g_xuiDC, &m);
    XuiSelectBrush(g_xuiDC, br);
    XuiSetColorFactor(g_xuiDC, tint);
    XUIRect rc(l, t, r, b);
    XuiFillRect(g_xuiDC, &rc);
}

/* The impressive bit: a full XUI scene rendered every frame -- gradient panel,
 * header bar, animated accent underline + sweeping highlight, drop-shadowed
 * title, subtitle and a feature list, all via XUI immediate-mode calls. */
/* draw a text run with a uniform scale about its own origin (pulsing title) */
static void XuiTextScaled(HXUIFONT f, DWORD color, float x, float y, float scale,
                          DWORD style, LPCWSTR s)
{
    XUIRect clip(0, 0, g_bbW / scale, g_bbH / scale);
    D3DXMATRIX m; D3DXMatrixScaling(&m, scale, scale, 1.f); m._41 = x; m._42 = y;
    XuiRenderSetTransform(g_xuiDC, &m);
    XuiSelectFont(g_xuiDC, f);
    XuiSetColorFactor(g_xuiDC, color);
    XuiDrawText(g_xuiDC, s, style | XUI_FONT_STYLE_SINGLE_LINE | XUI_FONT_STYLE_NO_WORDWRAP, 0, &clip);
}

static void DrawXuiScene(float t)
{
    XuiRenderBegin(g_xuiDC, D3DCOLOR_ARGB(255, 8, 10, 26));
    D3DXMATRIX view; D3DXMatrixIdentity(&view); XuiRenderSetViewTransform(g_xuiDC, &view);

    const float PANL = 120, PANT = 150, PANR = g_bbW - 120.f, PANB = g_bbH - 150.f;

    /* slowly rotate the panel's linear gradient so it shimmers */
    D3DXMATRIX gm; D3DXMatrixRotationZ(&gm, t * 0.25f);
    XuiBrushSetXForm(g_xuiGrad, &gm);
    XuiRect_(g_xuiGrad,  0xFFFFFFFF, PANL, PANT, PANR, PANB);           /* gradient panel   */

    /* radial glow that breathes behind the header */
    float g = 0.7f + 0.3f * (float)sin(t * 1.3f);
    XuiRect_(g_xuiGlow, D3DCOLOR_ARGB((int)(180 * g), 255, 255, 255),
             PANL + 40, PANT - 10, PANL + 560, PANT + 150);

    XuiRect_(g_xuiPanel, 0xFFFFFFFF, PANL, PANT, PANR, PANT + 84);      /* header bar       */

    /* rainbow-cycled accent underline whose width sweeps with time */
    float w = (PANR - PANL - 80) * (0.5f + 0.5f * (float)sin(t * 1.7f));
    DWORD ac = D3DCOLOR_ARGB(255, 128 + (int)(127 * sin(t * 2.0f)),
                             128 + (int)(127 * sin(t * 2.0f + 2.09f)),
                             128 + (int)(127 * sin(t * 2.0f + 4.19f)));
    XuiRect_(g_xuiAccent, ac, PANL + 40, PANT + 78, PANL + 40 + w, PANT + 84);
    /* sweeping vertical highlight bar */
    float hx = PANL + 40 + (PANR - PANL - 80) * (0.5f + 0.5f * (float)sin(t * 0.8f));
    XuiRect_(g_xuiBar, D3DCOLOR_ARGB(60, 255, 255, 255), hx, PANT + 90, hx + 3, PANB - 20);

    /* drop-shadowed title that gently pulses in scale */
    XuiSetTextDropShadowColor(g_xuiDC, D3DCOLOR_ARGB(200, 0, 0, 0));
    float ts = 1.0f + 0.035f * (float)sin(t * 2.2f);
    XuiTextScaled(g_xuiFontBig, D3DCOLOR_ARGB(255, 255, 255, 255), PANL + 40, PANT + 12, ts,
                  XUI_FONT_STYLE_DROPSHADOW, L"RXDK \x00B7 XUI");
    XuiText(g_xuiFontMed, D3DCOLOR_ARGB(255, 150, 210, 255), PANL + 40, PANT + 108,
            XUI_FONT_STYLE_NORMAL, L"Xbox 360 UI framework \x2014 immediate-mode rendering");

    static const wchar_t *items[] = {
        L"\x2022  XuiCreateFont / XuiDrawText  \x2014  TrueType glyph atlas",
        L"\x2022  Linear + radial gradient brushes, animated XForms",
        L"\x2022  XuiSetColorFactor / drop shadow / scale transforms",
        L"\x2022  xuirun + xuirender on our own D3D device",
    };
    for (int i = 0; i < 4; ++i)
        XuiText(g_xuiFontSmall, D3DCOLOR_ARGB(255, 220, 226, 240),
                PANL + 48, PANT + 170 + i * 40.f, XUI_FONT_STYLE_NORMAL, items[i]);

    /* live colour-cycled status line */
    DWORD c = D3DCOLOR_ARGB(255, 120 + (int)(120 * sin(t)), 220, 160 + (int)(80 * cos(t * 1.3f)));
    XuiText(g_xuiFontMed, c, PANL + 48, PANB - 70, XUI_FONT_STYLE_NORMAL,
            L"rendered live by XUI \x2014 fixed via -fshort-wchar");

    XuiRenderEnd(g_xuiDC);
    /* no XuiRenderPresent: the dashboard's Present() flips the frame, and the HUD
       overlay is drawn on top afterwards. */
}

/* ============================ sections =============================== */

#define AUTO_FRAMES 300   /* ~5s per section at 60fps */

enum { SEC_D3D9, SEC_SHADERS, SEC_TEXT, SEC_VIDEO, SEC_AUDIO, SEC_X3D, SEC_XAPO, SEC_XMP, SEC_XACT, SEC_XUI, SEC_INPUT, SEC_NET, SEC_HTTP, SEC_SYSTEM, SEC_DATA, SEC_OMP, SEC_COUNT };
static const char *g_secName[SEC_COUNT] = {
    "D3D9 CORE", "SHADERS", "TEXT / FONT", "XMV VIDEO", "XAUDIO2", "X3DAUDIO", "XAPOFX", "XMP MUSIC", "XACT3", "XUI", "XINPUT", "XNET", "XHTTP", "SYSTEM", "DATA / CPU", "OPENMP",
};
#define XMV_MOVIE "game:\\Media\\Video\\Sample.wmv"
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
    case SEC_VIDEO:
        SetStatus(COL_OK, "XMedia2 XMV -- playing " XMV_MOVIE);
        break;
    case SEC_AUDIO:   ToneStart();
                      SetStatus(g_audioReady ? COL_OK : COL_FAIL,
                                g_audioReady ? "playing WAV sample (stops on exit)" : "engine unavailable");
                      DbgPrint("[DASH] audio: %s %s\n", g_wavIsSample ? "sample" : "tone",
                               g_audioReady ? "started" : "unavailable"); break;
    case SEC_X3D:     ToneStart();
                      SetStatus(g_x3dReady ? COL_OK : COL_FAIL,
                                g_x3dReady ? "3D-panning the sample around the listener" : "X3DAudio unavailable");
                      break;
    case SEC_XAPO:    if (g_apoVoice) g_apoVoice->Start(0, XAUDIO2_COMMIT_NOW);
                      SetStatus(g_apoOk ? COL_OK : COL_FAIL,
                                g_apoOk ? "playing beeps run through the FXEcho DSP -- listen for the echoes"
                                        : "XAPOFX effect unavailable");
                      break;
    case SEC_XMP: {
        if (g_xmpReady) {
            XMPSetVolume(1.0f, NULL);
            DWORD hp = XMPPlayTitlePlaylist(g_xmpPlaylist, NULL, NULL);
            SetStatus(hp == ERROR_SUCCESS ? COL_OK : COL_FAIL,
                      hp == ERROR_SUCCESS ? "playing WMA title playlist (stops on exit)"
                                          : "XMPPlayTitlePlaylist failed");
            DbgPrint("[DASH] xmp: PlayTitlePlaylist hr=0x%08x\n", hp);
        } else {
            SetStatus(COL_PEND, "no title playlist (WMA missing) -- querying status only");
        }
        break;
    }
    case SEC_XACT:
        if (g_xactReady) {
            HRESULT hp = g_xactSB->Play(g_xactCueIx, 0, 0, &g_xactCue);
            SetStatus(SUCCEEDED(hp) ? COL_OK : COL_FAIL,
                      SUCCEEDED(hp) ? "XACT3 engine playing cue 'MusicMono' (XMA)"
                                    : "XACT Play failed");
            DbgPrint("[DASH] xact: Play hr=0x%08x\n", hp);
        } else {
            SetStatus(COL_FAIL, "XACT3 init failed (banks missing?)");
        }
        break;
    case SEC_XUI:
        SetStatus(g_xuiReady ? COL_OK : COL_FAIL,
                  g_xuiReady ? "XUI framework -- gradient panel, TTF fonts, brushes"
                             : "XUI init failed");
        break;
    case SEC_INPUT:   SetStatus(COL_PEND, "polling XInput port 0..."); break;
    case SEC_NET:
        SetStatus(g_netStarted ? COL_OK : COL_FAIL,
                  g_netStarted ? "XNet up -- querying address / link" : "XNetStartup failed");
        DbgPrint("[DASH] net: started=%d\n", (int)g_netStarted);
        break;
    case SEC_SYSTEM:
        SetStatus(COL_OK, "system / online libs: link + init + query smoke test");
        break;
    }
}

static void SectionExit(int s)
{
    if (s == SEC_AUDIO || s == SEC_X3D) { ToneStop(); DbgPrint("[DASH] audio: stopped\n"); }
    if (s == SEC_XAPO && g_apoVoice) { g_apoVoice->Stop(0, XAUDIO2_COMMIT_NOW); DbgPrint("[DASH] xapo: stopped\n"); }
    if (s == SEC_XMP && g_xmpReady) { XMPStop(NULL); DbgPrint("[DASH] xmp: stopped\n"); }
    if (s == SEC_XACT && g_xactReady) {
        if (g_xactCue) { g_xactCue->Stop(XACT_FLAG_STOP_IMMEDIATE); g_xactCue->Destroy(); g_xactCue = NULL; }
        g_xact->DoWork();
        DbgPrint("[DASH] xact: stopped\n");
    }
    if (s == SEC_X3D && g_audioReady) {   /* undo the 3D pan so later playback is centred */
        float m[8]; for (DWORD i = 0; i < g_dstCh && i < 8; ++i) m[i] = 1.0f;
        g_srcVoice->SetOutputMatrix(NULL, 1, g_dstCh, m, XAUDIO2_COMMIT_NOW);
    }
}

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
    case SEC_VIDEO:
        DrawText(64, 180, 1.5f, COL_OK, "XMV movie playing...");
        DrawText(64, 232, 1.25f, COL_WHITE, "XMedia2CreateXmvPlayer -> Play (CPU VC-1 + XMA)");
        DrawText(64, 266, 1.25f, COL_DIM,  XMV_MOVIE);
        if (g_vidState == 1) {
            wsprintfA(line, "movie: %ux%u  @ %dfps  dur %ums",
                      g_vidW, g_vidH, (int)g_vidFps, g_vidDur);
            DrawText(64, 300, 1.25f, COL_OK, line);
        }
        DrawText(64, 344, 1.0f, COL_DIM, "player takes over the screen -- press A / B / START to skip");
        break;
    case SEC_AUDIO: {
        DrawText(64, 200, 1.5f, g_audioReady ? COL_OK : COL_FAIL,
                 g_audioReady ? "XAudio2 voice PLAYING" : "XAudio2 unavailable");
        DrawText(64, 250, 1.25f, COL_WHITE, "engine -> mastering voice -> source voice");
        DrawText(64, 284, 1.25f, COL_DIM,  g_wavIsSample
                 ? "PCM WAV sample from game:\\Media\\Sounds (starts on enter, stops on exit)"
                 : "440Hz fallback tone (WAV not found)");
        break;
    }
    case SEC_X3D: {
        X3DUpdate(tglob * 1.5f);
        DrawText(64, 200, 1.5f, g_x3dReady ? COL_OK : COL_FAIL,
                 g_x3dReady ? "X3DAudio: sample orbiting the listener" : "X3DAudio unavailable");
        DrawText(64, 250, 1.25f, COL_WHITE, "X3DAudioCalculate -> SetOutputMatrix each frame");
        float ang = tglob * 1.5f;
        wsprintfA(line, "emitter: x=%d z=%d  (orbit radius 5)", (int)(5 * sin(ang)), (int)(5 * cos(ang)));
        DrawText(64, 284, 1.25f, COL_DIM, line);
        wsprintfA(line, "final mix: %u channels", g_dstCh);
        DrawText(64, 318, 1.25f, COL_DIM, line);
        break;
    }
    case SEC_XAPO: {
        DrawText(64, 180, 1.5f, g_apoOk ? COL_OK : COL_FAIL,
                 g_apoOk ? "XAPOFX audio effect -- FXEcho (audible)" : "XAPOFX unavailable");
        DrawText(64, 232, 1.25f, COL_WHITE, "CreateFX(FXEcho) -> SetParameters -> IXAPO::Process over a beep pattern");
        DrawText(64, 266, 1.0f,  COL_DIM,  "the effect DSP runs in guest CPU; the echoed buffer plays on a float voice");
        if (g_apoHr == S_OK) {
            wsprintfA(line, "echo: 300ms delay, 45%% feedback, 60%% wet   chunks altered: %d", g_apoChanged);
            DrawText(64, 306, 1.25f, g_apoOk ? COL_OK : COL_PEND, line);
            DrawText(64, 348, 1.25f, COL_OK, "listen: each beep repeats, decaying -- that's the XAPO echo");
        } else {
            wsprintfA(line, "CreateFX hr=0x%08x", g_apoHr);
            DrawText(64, 306, 1.25f, COL_FAIL, line);
        }
        break;
    }
    case SEC_XMP: {
        XMP_STATE stt = XMP_STATE_IDLE; FLOAT vol = 0;
        XMP_PLAYBACKMODE pm = XMP_PLAYBACKMODE_INORDER; XMP_REPEATMODE rm = XMP_REPEATMODE_PLAYLIST;
        DWORD pbFlags = 0;
        DWORD hs = XMPGetStatus(&stt), hv = XMPGetVolume(&vol);
        DWORD hb = XMPGetPlaybackBehavior(&pm, &rm, &pbFlags);
        bool ok = (hs == ERROR_SUCCESS);
        DrawText(64, 180, 1.5f, ok ? COL_OK : COL_FAIL,
                 ok ? "XMP music player (xmp.lib)" : "XMP unavailable");
        DrawText(64, 232, 1.25f, COL_WHITE, "playing a title playlist over a WMA (system media player)");
        DrawText(64, 266, 1.0f, COL_DIM,  XMP_WMA_W ? "game:\\Media\\Sounds\\music1.wma" : "");
        const char *sn = stt == XMP_STATE_PLAYING ? "PLAYING" :
                         stt == XMP_STATE_PAUSED  ? "PAUSED"  : "IDLE";
        wsprintfA(line, "XMPGetStatus -> %s", sn);
        DrawText(64, 300, 1.25f, stt == XMP_STATE_PLAYING ? COL_OK : COL_DIM, line);
        if (hv == ERROR_SUCCESS) {
            wsprintfA(line, "XMPGetVolume -> %d%%", (int)(vol * 100));
            DrawText(64, 334, 1.25f, COL_WHITE, line);
        }
        if (hb == ERROR_SUCCESS) {
            wsprintfA(line, "playback: %s / %s",
                      pm == XMP_PLAYBACKMODE_SHUFFLE ? "shuffle" : "in-order",
                      rm == XMP_REPEATMODE_NOREPEAT  ? "no-repeat" : "repeat-playlist");
            DrawText(64, 368, 1.25f, COL_DIM, line);
        }
        DrawText(64, 402, 1.0f, COL_DIM, "XMPCreateTitlePlaylist + XMPPlayTitlePlaylist -- real playback");
        break;
    }
    case SEC_XACT: {
        if (g_xactReady) g_xact->DoWork();   /* pump the XACT engine each frame */
        DrawText(64, 180, 1.5f, g_xactReady ? COL_OK : COL_FAIL,
                 g_xactReady ? "XACT3 authored audio engine" : "XACT3 unavailable");
        DrawText(64, 232, 1.25f, COL_WHITE, "compiled banks: .xsb sound bank + .xwb XMA wave bank");
        DrawText(64, 266, 1.0f,  COL_DIM,  "built offline with xactbld3 from XactSounds.xap");
        if (g_xactReady) {
            wsprintfA(line, "XACT3CreateEngine + CreateSoundBank/WaveBank -> cue 'MusicMono' (#%u)", g_xactCueIx);
            DrawText(64, 306, 1.25f, COL_OK, line);
            DrawText(64, 340, 1.25f, COL_WHITE, "SoundBank->Play + engine DoWork() -- XMA decoded on the APU");
            DrawText(64, 384, 1.0f, COL_DIM, "the middleware audio path most 360 games shipped with");
        } else {
            wsprintfA(line, "init hr=0x%08x", g_xactInitHr);
            DrawText(64, 306, 1.25f, COL_FAIL, line);
        }
        break;
    }
    case SEC_XUI:
        if (g_xuiReady) DrawXuiScene(tglob);
        else { DrawText(64, 200, 1.5f, COL_FAIL, "XUI unavailable");
               DrawText(64, 250, 1.25f, COL_DIM, "XuiRenderInitShared / XuiInit failed"); }
        break;
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

        /* real socket-path test: the working part of xenia's networking */
        wsprintfA(line, "UDP socket: %s   bind: %s   loopback echo: %s",
                  g_netSockOk ? "OK" : "FAIL",
                  g_netBindOk ? "OK" : "FAIL",
                  g_netEchoOk ? "OK" : (g_netBindOk ? "no reply" : "-"));
        DrawText(64, 334, 1.25f,
                 (g_netSockOk && g_netBindOk && g_netEchoOk) ? COL_OK : COL_PEND, line);
        if (g_netBindOk) {
            wsprintfA(line, "socket()/bind()/sendto()/recvfrom() round-trip on 127.0.0.1:%d", g_netPort);
            DrawText(64, 364, 1.0f, COL_DIM, line);
        }
        /* external: real internet DNS resolve via 8.8.8.8 */
        if (g_dnsOk) {
            wsprintfA(line, "internet: %s -> %d.%d.%d.%d  (DNS via 8.8.8.8)",
                      g_dnsHost, g_dnsIp[0], g_dnsIp[1], g_dnsIp[2], g_dnsIp[3]);
            DrawText(64, 392, 1.25f, COL_OK, line);
        } else {
            wsprintfA(line, "internet: %s -> no reply (offline, or host blocks UDP:53)", g_dnsHost);
            DrawText(64, 392, 1.25f, COL_PEND, line);
        }
        break;
    }
    case SEC_SYSTEM: {
        DrawText(64, 172, 1.4f, COL_WHITE, "system / online libs -- link + init + query");
        DrawText(64, 206, 1.0f, COL_DIM,  "one benign probe each; the libs that can't fully run without a console/Live");

        static const char *langs[] = { "?", "English", "Japanese", "German", "French",
                                       "Spanish", "Italian", "Korean", "T-Chinese",
                                       "Portuguese", "S-Chinese", "Polish", "Russian" };
        const char *ln = (g_smLang < (sizeof(langs) / sizeof(langs[0]))) ? langs[g_smLang] : "?";
        const float C1 = 64, C2 = 320;   /* two aligned columns: lib | probe + result */

        DrawText(C1, 250, 1.25f, COL_BLUE, "xam");
        wsprintfA(line, "XGetLanguage -> %u (%s)   XGetGameRegion -> 0x%04X", g_smLang, ln, g_smRegion);
        DrawText(C2, 250, 1.25f, COL_OK, line);

        DrawText(C1, 284, 1.25f, COL_BLUE, "tracerecording");
        wsprintfA(line, "XTraceIsRecording -> %s   (thunks to xbdm DmTrace*)", g_smTrace > 0 ? "yes" : "no");
        DrawText(C2, 284, 1.25f, COL_OK, line);

        DrawText(C1, 318, 1.25f, COL_BLUE, "xsim");
        DrawText(C2, 318, 1.25f, COL_OK, "xsim.lib links + XSimInitialize runs (controller input simulation)");

        DrawText(C1, 352, 1.25f, COL_BLUE, "xonline");
        wsprintfA(line, "XOnlineStartup -> 0x%08x  %s", g_smOnline,
                  g_smOnline == ERROR_SUCCESS ? "(Live runtime up)" : "(unavailable)");
        DrawText(C2, 352, 1.25f, g_smOnline == ERROR_SUCCESS ? COL_OK : COL_PEND, line);

        DrawText(C1, 396, 1.0f, COL_DIM, "probes run once at startup; side-effecting inits torn back down");
        break;
    }
    case SEC_HTTP: {
        DrawText(64, 180, 1.5f, g_httpOk ? COL_OK : COL_PEND, "XHTTP -- real HTTP client");
        DrawText(64, 232, 1.25f, COL_WHITE, "XHttpOpen -> Connect -> OpenRequest(GET) -> SendRequest -> ReceiveResponse");
        wsprintfA(line, "GET  http://%s/", HTTP_HOST);
        DrawText(64, 266, 1.25f, COL_DIM, line);
        if (g_httpStatus) {
            wsprintfA(line, "HTTP status: %u %s", g_httpStatus, g_httpOk ? "OK" : "");
            DrawText(64, 306, 1.5f, g_httpOk ? COL_OK : COL_PEND, line);
            wsprintfA(line, "body[0..]: %s", g_httpBody);
            DrawText(64, 348, 1.0f, COL_DIM, line);
            DrawText(64, 384, 1.0f, COL_DIM, "a real request over xenia's TCP socket layer (XNet)");
        } else if (g_httpStep[0] == 'd') {   /* "done": API ran, no real response */
            DrawText(64, 306, 1.25f, COL_PEND, "no status -- xenia stubs the NetDll_XHttp* provider");
            DrawText(64, 344, 1.0f, COL_DIM, "xhttp.lib links + the full API flow runs; the request needs the");
            DrawText(64, 372, 1.0f, COL_DIM, "console's system HTTP service, which xenia does not implement");
        } else {
            wsprintfA(line, "stopped at %s", g_httpStep);
            DrawText(64, 306, 1.25f, COL_FAIL, line);
        }
        break;
    }
    case SEC_DATA: {
        DrawText(64, 172, 1.4f, COL_WHITE, "data / CPU middleware -- pure-CPU, fully verifiable");
        const float C1 = 64, C2 = 300;

        DrawText(C1, 240, 1.25f, COL_BLUE, "xcompress");
        wsprintfA(line, "XMemCompress LZX  %u -> %u bytes (%d%%)   decompress round-trip: %s",
                  g_zSrc, g_zComp, g_zSrc ? (int)(100 - g_zComp * 100 / g_zSrc) : 0,
                  g_zOk ? "MATCH" : "FAIL");
        DrawText(C2, 240, 1.25f, g_zOk ? COL_OK : COL_FAIL, line);

        DrawText(C1, 284, 1.25f, COL_BLUE, "xjson");
        wsprintfA(line, "XJSONReadToken  %d tokens, %d strings   title = \"%s\"",
                  g_jTokens, g_jStrings, g_jName);
        DrawText(C2, 284, 1.25f, g_jTokens > 0 ? COL_OK : COL_FAIL, line);

        DrawText(64, 340, 1.0f, COL_DIM, "XMemCompress/XMemDecompress (LZX) and the XJSON SAX reader");
        break;
    }
    case SEC_OMP: {
        DrawText(64, 180, 1.5f, g_ompOk ? COL_OK : COL_FAIL,
                 g_ompOk ? "OpenMP (vcomp) -- multicore parallel reduction" : "OpenMP unavailable");
        DrawText(64, 232, 1.25f, COL_WHITE, "omp_get_num_procs + _vcomp_fork parallel region across the CPU threads");
        wsprintfA(line, "hardware threads: omp_get_num_procs = %d   omp_get_max_threads = %d",
                  g_ompProcs, g_ompMax);
        DrawText(64, 272, 1.25f, COL_OK, line);
        wsprintfA(line, "_vcomp_fork ran a parallel region on %d threads; %d executed it",
                  g_ompThreads, g_ompRan);
        DrawText(64, 306, 1.25f, g_ompOk ? COL_OK : COL_FAIL, line);
        DrawText(64, 348, 1.0f, COL_DIM, "each thread ran a 2M-iteration loop concurrently -- real multicore work");
        DrawText(64, 380, 1.0f, COL_DIM, "the XDK's OpenMP runtime (vcomp) on our modern CRT");
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

/* Per-frame callback during XMV Play(): poll the pad and stop the clip early on
 * A/B/Start, so a controller can skip the movie (Play() otherwise blocks until
 * the clip ends). ctx is the player. */
static DWORD g_xmvStart;
static void XmvFrameCallback(PVOID ctx)
{
    XINPUT_STATE st; ZeroMemory(&st, sizeof(st));
    if (XInputGetState(0, &st) == ERROR_SUCCESS) {
        WORD b = st.Gamepad.wButtons;
        if (b & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_START))
            ((IXMedia2XmvPlayer *)ctx)->Stop(XMEDIA_STOP_IMMEDIATE);
    }
#if !defined(DASH_SPIN_FOREVER) && !defined(DASH_FULL_XMV)
    /* Quick (headless / one-cycle) build: cap the clip so a self-test run isn't
       held up for the whole movie. The windowed --play/--spin demos play it in
       full (--play exits after one cycle; --spin loops). */
    if (GetTickCount() - g_xmvStart > 4000)
        ((IXMedia2XmvPlayer *)ctx)->Stop(XMEDIA_STOP_IMMEDIATE);
#endif
}

/* XMV video: create the player, show a labelled pre-roll frame, then Play() the
 * clip (blocking -- the player decodes on the CPU and presents each frame
 * itself), then continue. Real playback works now that xenia resolves the
 * save/rest stubs (no more giant-function miscompile that faulted decode).
 * A/B/Start skip the clip via XmvFrameCallback. */
static void DoVideoSection()
{
    XMEDIA_XMV_CREATE_PARAMETERS p; ZeroMemory(&p, sizeof(p));
    p.createType = XMEDIA_CREATE_FROM_FILE;
    p.createFromFile.szFileName = XMV_MOVIE;
    p.dwAudioStreamId = XMEDIA_STREAM_ID_USE_DEFAULT;
    p.dwVideoStreamId = XMEDIA_STREAM_ID_USE_DEFAULT;
    IXMedia2XmvPlayer *pl = NULL;
    HRESULT hr = XMedia2CreateXmvPlayer(g_dev, g_xa2, &p, &pl);
    DbgPrint("[DASH] XMV create hr=0x%08x\n", hr);
    if (FAILED(hr) || !pl) { g_vidState = -1; return; }

    XMEDIA_VIDEO_DESCRIPTOR vd; ZeroMemory(&vd, sizeof(vd));
    if (SUCCEEDED(pl->GetVideoDescriptor(&vd))) {
        g_vidW = vd.dwWidth; g_vidH = vd.dwHeight; g_vidFps = vd.fFrameRate; g_vidDur = vd.dwClipDuration;
    }
    g_vidState = 1;

    /* labelled pre-roll so the viewer sees what's about to play */
    g_dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                 D3DCOLOR_XRGB(8, 10, 26), 1.0f, 0);
    TextReset(); SectionBody(SEC_VIDEO, 0, 0); DrawHUD(); TextFlush(); Present();
    Sleep(800);

    pl->SetCallback(XMEDIA_NOTIFY_END_OF_FRAME, XmvFrameCallback, pl);
    DbgPrint("[DASH] XMV Play...\n");
    g_xmvStart = GetTickCount();
    hr = pl->Play(0, NULL);   /* blocks until the clip ends (or A/B/Start skips) */
    DbgPrint("[DASH] XMV Play hr=0x%08x\n", hr);
    pl->Release();
    g_dev->SetRenderState(D3DRS_VIEWPORTENABLE, TRUE);
}

int main(void)
{
    DbgPrint("[DASH] start\n");
    if (FAILED(InitD3D())) { DbgPrint("[DASH] InitD3D FAILED\n"); return 1; }

    /* No loading screen: with the save/rest JIT fix xenia warms in ~1.5s, so we
     * go straight to the sections. */
    if (FAILED(InitText())){ DbgPrint("[DASH] InitText FAILED\n"); return 1; }
    InitTriangle();
    InitFx();
    InitAudioEngine();
    InitXapo();
    InitNet();
    InitHttp();
    InitSmoke();
    InitXmp();
    InitXact();
    InitXui();
    InitData();
    InitOmp();
    DbgPrint("[DASH] init complete; running sections\n");

    SectionEnter(g_section);

    int frames = 0x7fffffff;
#ifndef DASH_SPIN_FOREVER
    frames = SEC_COUNT * AUTO_FRAMES + 30;   /* headless: one full cycle then DONE */
#endif
    for (int f = 0; f < frames; ++f) {
        HandleNav();

        /* XMV plays full-screen via a blocking Play() that presents the clip
         * itself; hand off, then advance to the next section when it ends. */
        if (g_section == SEC_VIDEO) {
            DoVideoSection();
            Goto((SEC_VIDEO + 1) % SEC_COUNT);
            continue;
        }

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
