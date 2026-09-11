/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 *
 * Spinning-triangle bring-up for the RXDK-360 toolchain: the canonical XDK
 * sample, built with our clang + ld.lld against the stock XDK D3D9 headers,
 * using D3DX matrix math instead of <xboxmath.h> (whose VMX128 intrinsics our
 * clang does not yet expose). Progress is mirrored to DbgPrint so a headless
 * xenia run is self-verifying; run xenia with a window to watch it spin.
 */
#include <xtl.h>

extern "C" int DbgPrint(const char *, ...);

static const char *g_vs =
    " float4x4 matWVP : register(c0);                 "
    " struct VS_IN  { float4 Pos : POSITION; float4 Color : COLOR; }; "
    " struct VS_OUT { float4 Pos : POSITION; float4 Color : COLOR; }; "
    " VS_OUT main( VS_IN In ) {                       "
    "     VS_OUT Out;                                 "
    "     Out.Pos = mul( matWVP, In.Pos );            "
    "     Out.Color = In.Color;                       "
    "     return Out;                                 "
    " }                                               ";

static const char *g_ps =
    " struct PS_IN { float4 Color : COLOR; };         "
    " float4 main( PS_IN In ) : COLOR { return In.Color; } ";

struct COLORVERTEX { float Position[3]; DWORD Color; };

static D3DDevice           *g_dev;
static D3DVertexBuffer     *g_vb;
static D3DVertexDeclaration*g_decl;
static D3DVertexShader     *g_vsh;
static D3DPixelShader      *g_psh;
static D3DXMATRIX           g_proj, g_view;

static HRESULT InitD3D()
{
    DbgPrint("[TRI] Direct3DCreate9...\n");
    Direct3D *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    DbgPrint("[TRI] d3d=%p\n", d3d);
    if (!d3d) return E_FAIL;

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    XVIDEO_MODE vm;
    ZeroMemory(&vm, sizeof(vm));
    DbgPrint("[TRI] XGetVideoMode...\n");
    XGetVideoMode(&vm);
    DbgPrint("[TRI] video %dx%d wide=%d; CreateDevice...\n",
             (int)vm.dwDisplayWidth, (int)vm.dwDisplayHeight, (int)vm.fIsWideScreen);
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
    DbgPrint("[TRI] CreateDevice hr=0x%08x w=%d h=%d\n", hr,
             (int)pp.BackBufferWidth, (int)pp.BackBufferHeight);
    return hr;
}

static HRESULT InitScene()
{
    ID3DXBuffer *code = NULL, *err = NULL;
    HRESULT hr = D3DXCompileShader(g_vs, (UINT)strlen(g_vs), NULL, NULL,
                                   "main", "vs_3_0", 0, &code, &err, NULL);
    DbgPrint("[TRI] compile VS hr=0x%08x\n", hr);
    if (FAILED(hr)) { if (err) DbgPrint("[TRI] VS err: %s\n", (char*)err->GetBufferPointer()); return hr; }
    g_dev->CreateVertexShader((DWORD*)code->GetBufferPointer(), &g_vsh);

    hr = D3DXCompileShader(g_ps, (UINT)strlen(g_ps), NULL, NULL,
                           "main", "ps_3_0", 0, &code, &err, NULL);
    DbgPrint("[TRI] compile PS hr=0x%08x\n", hr);
    if (FAILED(hr)) { if (err) DbgPrint("[TRI] PS err: %s\n", (char*)err->GetBufferPointer()); return hr; }
    g_dev->CreatePixelShader((DWORD*)code->GetBufferPointer(), &g_psh);

    D3DVERTEXELEMENT9 elems[] = {
        { 0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
        D3DDECL_END()
    };
    g_dev->CreateVertexDeclaration(elems, &g_decl);

    hr = g_dev->CreateVertexBuffer(3 * sizeof(COLORVERTEX), D3DUSAGE_WRITEONLY,
                                   0, D3DPOOL_MANAGED, &g_vb, NULL);
    if (FAILED(hr)) { DbgPrint("[TRI] CreateVB hr=0x%08x\n", hr); return hr; }

    COLORVERTEX verts[] = {
        {  0.0f, -1.1547f, 0.0f, 0xffff0000 },
        { -1.0f,  0.5777f, 0.0f, 0xff00ff00 },
        {  1.0f,  0.5777f, 0.0f, 0xffffff00 },
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
                 D3DCOLOR_XRGB(0, 0, 64), 1.0f, 0);
    g_dev->SetVertexDeclaration(g_decl);
    g_dev->SetStreamSource(0, g_vb, 0, sizeof(COLORVERTEX));
    g_dev->SetVertexShader(g_vsh);
    g_dev->SetPixelShader(g_psh);

    D3DXMATRIX world, wvp, tmp;
    D3DXMatrixRotationZ(&world, angle);
    D3DXMatrixMultiply(&tmp, &world, &g_view);
    D3DXMatrixMultiply(&wvp, &tmp, &g_proj);
    g_dev->SetVertexShaderConstantF(0, (float*)&wvp, 4);

    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
    g_dev->Present(NULL, NULL, NULL, NULL);
}

int main(void)
{
    DbgPrint("[TRI] start\n");
    if (FAILED(InitD3D()))  { DbgPrint("[TRI] InitD3D FAILED\n");  return 1; }
    if (FAILED(InitScene())){ DbgPrint("[TRI] InitScene FAILED\n"); return 1; }
    DbgPrint("[TRI] scene ready; rendering\n");
    for (int f = 0; f < 240; ++f) {
        Render((float)f * 0.03f);
        if (f % 60 == 0) DbgPrint("[TRI] frame %d presented\n", f);
    }
    DbgPrint("[TRI] DONE\n");
    return 0;
}
