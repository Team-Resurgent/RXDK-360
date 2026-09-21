/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RXDK-360 FXL C++ ABI bridge.
 *
 * FXL (the Fast eXtensible effect Library, fxl.h/fxl.inl) is header-inline, but
 * a few internal routines are real functions compiled into xgraphics.lib. Those
 * are C++ (not extern "C"), so clang references them with Itanium mangling while
 * the MSVC-built library exports the MSVC name - the reference goes unresolved
 * (e.g. FXLSetShaders, pulled in by FXLEffect_BeginTechnique). Bridge each one
 * with a thin forwarder whose C++ signature reproduces the Itanium name clang
 * emits, calling the MSVC symbol through an asm label. Being in libc.a the
 * forwarder is pulled only when a title actually references the FXL routine.
 */

/* Opaque tags: only the type NAMES feed the Itanium mangling, so a forward
 * declaration is enough and avoids dragging the full XDK headers in here. */
struct D3DDevice;
struct _FXLSHADERSTATEENTRY;

extern "C" void FXLSetShaders_msvc(D3DDevice*, _FXLSHADERSTATEENTRY*)
    __asm__("?FXLSetShaders@@YAXPAUD3DDevice@@PAU_FXLSHADERSTATEENTRY@@@Z");

/* Mangles to _Z13FXLSetShadersP9D3DDeviceP20_FXLSHADERSTATEENTRY. */
void FXLSetShaders(D3DDevice* pDevice, _FXLSHADERSTATEENTRY* pShaderStateData)
{
    FXLSetShaders_msvc(pDevice, pShaderStateData);
}
