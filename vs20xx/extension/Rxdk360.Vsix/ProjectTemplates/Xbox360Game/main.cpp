// $projectname$ - an Xbox 360 game skeleton built with the stock XDK on modern
// Visual Studio. Brings up a Direct3D 9 device and runs a clear/present loop.
#include <xtl.h>
#pragma comment(lib, "d3d9.lib")

extern "C" int DbgPrint(const char *, ...);

static D3DDevice *g_dev;

int main()
{
    Direct3D *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { DbgPrint("$projectname$: Direct3DCreate9 failed\n"); return 1; }

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    XVIDEO_MODE vm; ZeroMemory(&vm, sizeof(vm)); XGetVideoMode(&vm);
    pp.BackBufferWidth        = vm.dwDisplayWidth  < 1280 ? vm.dwDisplayWidth  : 1280;
    pp.BackBufferHeight       = vm.dwDisplayHeight <  720 ? vm.dwDisplayHeight :  720;
    pp.BackBufferFormat       = D3DFMT_X8R8G8B8;
    pp.BackBufferCount        = 1;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval   = D3DPRESENT_INTERVAL_ONE;

    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev);
    if (FAILED(hr)) { DbgPrint("$projectname$: CreateDevice hr=0x%08x\n", hr); return 1; }
    DbgPrint("$projectname$: device %dx%d up\n", pp.BackBufferWidth, pp.BackBufferHeight);

    // Render loop: clear to Xbox blue and present.
    for (;;)
    {
        g_dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                     D3DCOLOR_XRGB(0, 40, 90), 1.0f, 0);

        // ... your rendering here ...

        g_dev->Present(NULL, NULL, NULL, NULL);
    }
}
