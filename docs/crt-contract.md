# The CRT contract

The complete set of C runtime symbols the shipped Xbox 360 libraries reference,
derived by intersecting the undefined externals of `d3d9`, `xgraphics`, `xapilib`,
`xuirun`, `st`, `xact3`, `xnet` and `xonline` with what `libcMT.lib` and
`libcpMT.lib` define.

**164 symbols.** All of them come from `libcMT.lib`; `libcpMT.lib`
contributes nothing unique. Nothing here requires C++ exception or RTTI support --
there is no `__CxxFrameHandler`, no `_CxxThrowException` and no `type_info`, because
the shipped libraries are built with both disabled.

This is the surface a replacement CRT has to provide in order to link against the
platform libraries.

### Register save/restore helpers (72)

Hand-written assembly. Out-of-line prologue and epilogue helpers; no calling convention is involved, so these are unaffected by the ABI differences. The `vmx` variants save vr108-vr127, the platform's callee-saved vector registers.

```
__restfpr_14 __restfpr_18 __restfpr_19 __restfpr_20 __restfpr_21 __restfpr_22 
__restfpr_23 __restfpr_24 __restfpr_25 __restfpr_26 __restfpr_27 __restfpr_28 
__restgprlr_14 __restgprlr_15 __restgprlr_16 __restgprlr_17 __restgprlr_18 
__restgprlr_19 __restgprlr_20 __restgprlr_21 __restgprlr_22 __restgprlr_23 
__restgprlr_24 __restgprlr_25 __restgprlr_26 __restgprlr_27 __restgprlr_28 
__restgprlr_29 __restvmx_111 __restvmx_112 __restvmx_115 __restvmx_118 
__restvmx_120 __restvmx_122 __restvmx_123 __restvmx_124 __savefpr_14 
__savefpr_18 __savefpr_19 __savefpr_20 __savefpr_21 __savefpr_22 __savefpr_23 
__savefpr_24 __savefpr_25 __savefpr_26 __savefpr_27 __savefpr_28 
__savegprlr_14 __savegprlr_15 __savegprlr_16 __savegprlr_17 __savegprlr_18 
__savegprlr_19 __savegprlr_20 __savegprlr_21 __savegprlr_22 __savegprlr_23 
__savegprlr_24 __savegprlr_25 __savegprlr_26 __savegprlr_27 __savegprlr_28 
__savegprlr_29 __savevmx_111 __savevmx_112 __savevmx_115 __savevmx_118 
__savevmx_120 __savevmx_122 __savevmx_123 __savevmx_124
```

### Identical under both conventions (76)

Pure pointer/integer or pure floating-point signatures. A straightforward implementation compiled for the Xbox 360 target will interoperate directly.

```
??2@YAPAXI@Z ??3@YAXPAX@Z ??_U@YAPAXI@Z ??_V@YAXPAX@Z _FPinit _HUGE 
_RtlCheckStack12 __iob_func __onexitbegin __onexitend __security_check_cookie 
__security_cookie __u64tod _blkmov _chgsign _copysign _finite _fltused 
_fpclass _isnan _itow_s _purecall _strcmpi _stricmp _strnicmp _wcsicmp 
_wcslwr_s _wcsnicmp acos atexit atol bsearch calloc ceil cos exp floor fputs 
free isdigit log log10 malloc memcpy memmove memset pow qsort rand sin srand 
strcat_s strchr strcpy_s strncat_s strncmp strncpy strncpy_s strnlen strrchr 
strstr strtoul tan tolower wcscat wcscat_s wcschr wcscmp wcscpy wcscpy_s 
wcslen wcsncmp wcsncpy_s wcsstr wcstod wcstol
```

### Divergent -- need shims (16)

Varargs (the platform passes unnamed floating-point arguments in GPRs as well as FPRs), `double`-then-integer signatures where the GPR index differs, and the setjmp buffer layout. Each needs a hand-written thunk or a layout-compatible implementation.

```
__jump_unwind _cexit _mtinit _snprintf _snwprintf _vsnprintf fprintf ldexp 
longjmp modf printf setjmp sprintf swscanf_s vprintf vsprintf_s
```

## Notes

- `_fltused` is a linker-visible marker rather than a function.
- `__onexitbegin` / `__onexitend` are data symbols.
- `setjmp` / `longjmp` additionally require a `jmp_buf` layout matching the
  platform's, which has not been decoded yet.
