/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * Minimal standalone XMV playback repro: D3D9 device + XAudio2 + XMedia2 player
 * on game:\Media\Video\Sample.wmv, then a blocking Play(). Deliberately tiny so
 * it warms up fast and isolates the XMV path -- used to diagnose the Play()
 * host-fault under xenia (run under the Checked build for asserts).
 */
#include <xtl.h>
#include <xaudio2.h>
#include <xmedia2.h>

extern "C" int DbgPrint(const char *, ...);

int main(void)
{
    DbgPrint("[XMV] start\n");
    Direct3D *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { DbgPrint("[XMV] no d3d\n"); return 1; }

    D3DPRESENT_PARAMETERS pp; ZeroMemory(&pp, sizeof(pp));
    XVIDEO_MODE vm; ZeroMemory(&vm, sizeof(vm)); XGetVideoMode(&vm);
    pp.BackBufferWidth  = vm.dwDisplayWidth  < 1280 ? vm.dwDisplayWidth  : 1280;
    pp.BackBufferHeight = vm.dwDisplayHeight <  720 ? vm.dwDisplayHeight :  720;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    D3DDevice *dev = NULL;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    DbgPrint("[XMV] CreateDevice hr=0x%08x\n", hr);
    if (FAILED(hr)) return 1;

    IXAudio2 *xa2 = NULL;
    hr = XAudio2Create(&xa2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    DbgPrint("[XMV] XAudio2Create hr=0x%08x\n", hr);
    IXAudio2MasteringVoice *mv = NULL;
    if (SUCCEEDED(hr)) hr = xa2->CreateMasteringVoice(&mv, XAUDIO2_DEFAULT_CHANNELS,
                                                      XAUDIO2_DEFAULT_SAMPLERATE, 0, 0, NULL);
    DbgPrint("[XMV] CreateMasteringVoice hr=0x%08x\n", hr);

    XMEDIA_XMV_CREATE_PARAMETERS p; ZeroMemory(&p, sizeof(p));
    p.createType = XMEDIA_CREATE_FROM_FILE;
    p.createFromFile.szFileName = "game:\\Media\\Video\\Sample.wmv";
    p.dwAudioStreamId = XMEDIA_STREAM_ID_USE_DEFAULT;
    p.dwVideoStreamId = XMEDIA_STREAM_ID_USE_DEFAULT;

    IXMedia2XmvPlayer *pl = NULL;
    hr = XMedia2CreateXmvPlayer(dev, xa2, &p, &pl);
    DbgPrint("[XMV] CreateXmvPlayer hr=0x%08x pl=%p\n", hr, pl);
    if (SUCCEEDED(hr) && pl) {
        XMEDIA_VIDEO_DESCRIPTOR vd; ZeroMemory(&vd, sizeof(vd));
        if (SUCCEEDED(pl->GetVideoDescriptor(&vd)))
            DbgPrint("[XMV] descriptor %ux%u dur=%ums\n", vd.dwWidth, vd.dwHeight, vd.dwClipDuration);
        DbgPrint("[XMV] Play...\n");
        hr = pl->Play(0, NULL);
        DbgPrint("[XMV] Play hr=0x%08x\n", hr);
        pl->Release();
    }
    DbgPrint("[XMV] DONE\n");
    return 0;
}
