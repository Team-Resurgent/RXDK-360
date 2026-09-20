/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RXDK-360 <vadefs.h> for the modern (clang) toolchain - replaces the stock XDK
 * vadefs.h.
 *
 * The XDK models va_list as `char *` and implements _crt_va_start/_crt_va_arg with
 * MS-style pointer arithmetic (address of the last named argument + its size).
 * That is wrong for the PowerPC ELF ABI that clang compiles to: variadic arguments
 * are not laid out contiguously after the last named parameter (they use the
 * register save area / parameter save area), so the XDK macros would read garbage
 * from a clang-compiled variadic function.
 *
 * Since every title is now compiled by clang, va_list must be clang's builtin and
 * va_start/va_arg/va_end must be the compiler builtins that know the ABI. The XDK's
 * <stdarg.h> maps va_start -> _crt_va_start (etc.), so defining the _crt_va_*
 * families here (and being found ahead of the XDK's vadefs.h on the include path)
 * makes both <stdarg.h> and any direct vadefs users get the correct varargs.
 */
#ifndef _INC_VADEFS
#define _INC_VADEFS

#ifndef _VA_LIST_DEFINED
typedef __builtin_va_list va_list;
#define _VA_LIST_DEFINED
#endif

#define _crt_va_start(ap, v)  __builtin_va_start((ap), (v))
#define _crt_va_arg(ap, t)    __builtin_va_arg((ap), t)
#define _crt_va_end(ap)       __builtin_va_end((ap))
#define _crt_va_copy(d, s)    __builtin_va_copy((d), (s))

#endif /* _INC_VADEFS */
