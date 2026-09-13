// $projectname$ - an Xbox 360 game built with the RXDK-360 toolchain. Brings up
// a Direct3D 9 device and draws a spinning, vertex-coloured triangle - the
// canonical XDK bring-up - so a fresh project renders real geometry (not just a
// cleared frame). Builds a bootable .xex with either toolchain: Modern
// (clang/LLVM) or Legacy (stock XDK). Run it in Xenia (Ctrl+F5) or deploy to a
// devkit.
#include <xtl.h>
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

// The Xbox 360 GPU has no fixed-function pipeline, so even a triangle needs a
// vertex + pixel shader. These are precompiled offline with the XDK's fxc.exe
// into GPU microcode (shaders\tri_{vs,ps}.h define g_vs_bin / g_ps_bin) - the
// same way shipping titles do it, and it avoids the heavyweight runtime HLSL
// compiler. The HLSL sources are in shaders\tri_{vs,ps}.hlsl for reference.
#include "shaders/tri_vs.h"   // const DWORD g_vs_bin[]
#include "shaders/tri_ps.h"   // const DWORD g_ps_bin[]

extern "C" int DbgPrint(const char *, ...);

struct COLORVERTEX { float Position[3]; DWORD Color; };

static D3DDevice            *g_dev;
static D3DVertexBuffer      *g_vb;
static D3DVertexDeclaration *g_decl;
static D3DVertexShader      *g_vsh;
static D3DPixelShader       *g_psh;
static D3DXMATRIX            g_proj, g_view;

static HRESULT InitD3D()
{
    Direct3D *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { DbgPrint("$projectname$: Direct3DCreate9 failed\n"); return E_FAIL; }

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
    if (FAILED(hr)) { DbgPrint("$projectname$: CreateDevice hr=0x%08x\n", hr); return hr; }
    DbgPrint("$projectname$: device %dx%d up\n", pp.BackBufferWidth, pp.BackBufferHeight);
    return S_OK;
}

static HRESULT InitScene()
{
    HRESULT hr = g_dev->CreateVertexShader((const DWORD*)g_vs_bin, &g_vsh);
    if (FAILED(hr)) { DbgPrint("$projectname$: CreateVertexShader hr=0x%08x\n", hr); return hr; }
    hr = g_dev->CreatePixelShader((const DWORD*)g_ps_bin, &g_psh);
    if (FAILED(hr)) { DbgPrint("$projectname$: CreatePixelShader hr=0x%08x\n", hr); return hr; }

    D3DVERTEXELEMENT9 elems[] = {
        { 0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
        D3DDECL_END()
    };
    g_dev->CreateVertexDeclaration(elems, &g_decl);

    hr = g_dev->CreateVertexBuffer(3 * sizeof(COLORVERTEX), D3DUSAGE_WRITEONLY,
                                   0, D3DPOOL_MANAGED, &g_vb, NULL);
    if (FAILED(hr)) { DbgPrint("$projectname$: CreateVertexBuffer hr=0x%08x\n", hr); return hr; }

    // An equilateral triangle centred on the origin, one colour per corner.
    COLORVERTEX verts[] = {
        {  0.0f, -1.1547f, 0.0f, 0xffff0000 },   // top    - red
        { -1.0f,  0.5777f, 0.0f, 0xff00ff00 },   // left   - green
        {  1.0f,  0.5777f, 0.0f, 0xffffff00 },   // right  - yellow
    };
    void *p = NULL;
    g_vb->Lock(0, 0, &p, 0);
    memcpy(p, verts, sizeof(verts));
    g_vb->Unlock();

    D3DXMatrixPerspectiveFovLH(&g_proj, D3DX_PI / 4.0f, 16.0f / 9.0f, 1.0f, 200.0f);
    D3DXVECTOR3 eye(0, 0, -7), at(0, 0, 0), up(0, 1, 0);
    D3DXMatrixLookAtLH(&g_view, &eye, &at, &up);
    return S_OK;
}

static void Render(float angle)
{
    g_dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                 D3DCOLOR_XRGB(0, 40, 90), 1.0f, 0);
    g_dev->SetVertexDeclaration(g_decl);
    g_dev->SetStreamSource(0, g_vb, 0, sizeof(COLORVERTEX));
    g_dev->SetVertexShader(g_vsh);
    g_dev->SetPixelShader(g_psh);

    // world * view * proj, passed to the vertex shader as constant c0.
    D3DXMATRIX world, wvp, tmp;
    D3DXMatrixRotationZ(&world, angle);
    D3DXMatrixMultiply(&tmp, &world, &g_view);
    D3DXMatrixMultiply(&wvp, &tmp, &g_proj);
    g_dev->SetVertexShaderConstantF(0, (float*)&wvp, 4);

    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
    g_dev->Present(NULL, NULL, NULL, NULL);
}

int main()
{
    if (FAILED(InitD3D()))   return 1;
    if (FAILED(InitScene())) return 1;
    DbgPrint("$projectname$: scene ready; rendering\n");

    for (int f = 0; ; ++f)
    {
        Render((float)f * 0.03f);
        // ... your game logic here ...
    }
}
