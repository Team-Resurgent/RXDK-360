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

/* ---- kernel imports (xboxkrnl) ----
   The 360 kernel exports the full NT-family SEH/unwind surface (confirmed in
   xboxkrnl.lib): the dispatch below is Microsoft's own model -- our language
   handler is invoked by the kernel exception dispatcher for each frame, and on
   a match we drive the second pass + control transfer through RtlUnwind2, the
   same primitive MS's __CxxFrameHandler uses. No hand-rolled unwinder, no ABI
   workarounds; this is exactly how the shipped cl.exe libs already expect to be
   dispatched. */
extern void RaiseException(unsigned code, unsigned flags, unsigned n_args,
                           const unsigned *args);
extern void RtlUnwind2(unsigned TargetFrame, unsigned TargetIp,
                       void *ExceptionRecord, unsigned ReturnValue,
                       void *ContextRecord, void *HistoryTable);
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
typedef int EXCEPTION_DISPOSITION;
#define ExceptionContinueSearch    1
#define ExceptionContinueExecution 0

/* NT/Xenon EXCEPTION_RECORD: the C++ throw carries {magic, pObject, pThrowInfo}
   in ExceptionInformation (info[0..2]); info[1] is the thrown object pointer. */
typedef struct { unsigned code, flags, rec, addr, nparm; unsigned info[15]; }
    EXCEPTION_RECORD;

/* DISPATCHER_CONTEXT as the Xenon SEH dispatcher passes it. ControlPc (the
   in-frame PC), the RUNTIME_FUNCTION, the establisher frame, the CONTEXT being
   dispatched, and HandlerData (== our FuncInfo*, read by the kernel from
   FuncStart-4). The kernel also passes EstablisherFrame + ContextRecord as the
   2nd/3rd handler args (the __C_specific_handler shape in <xbox/excpt.h>), so we
   rely on those for the transfer and use dc only for HandlerData/ControlPc. */
typedef struct {
    unsigned ControlPc;
    unsigned ImageBase;
    unsigned FunctionEntry;                       /* RUNTIME_FUNCTION* */
    unsigned EstablisherFrame;
    unsigned ContextRecord;                       /* CONTEXT* */
    unsigned LanguageHandler;
    unsigned HandlerData;                         /* == FuncInfo* */
} DISPATCHER_CONTEXT;

/* Invoke an MSVC catch/cleanup funclet with the establisher frame pointer in
   r12 (the ABI the cl.exe-emitted funclets require: they recover the frame
   pointer as `addi r31, r12, -framesize` -- verified against a real object in
   tests/eh). Catch funclets return the continuation IP in r3; cleanup funclets
   return nothing. This one register-setup step is the only thing that must be
   expressed in asm; everything else is the portable table walk below. */
static unsigned rxdk_eh_call_funclet(unsigned funclet, unsigned establisher) {
    register unsigned r3  __asm__("r3");
    register unsigned r11 __asm__("r11") = funclet;
    register unsigned r12 __asm__("r12") = establisher;
    __asm__ __volatile__(
        "mtctr %2\n\t"
        "bctrl\n\t"
        : "=r"(r3)
        : "r"(r12), "r"(r11)
        : "ctr", "lr", "r0", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
          "memory", "cc");
    return r3;
}

/* ip2state: the active EH state at `pc` is the entry with the greatest ip <= pc.
   Callers pass the throwing call site (return address - 1). Entry = {ip, state}. */
static int rxdk_state_from_ip(const FuncInfo *fi, unsigned pc) {
    const unsigned *m = (const unsigned *)(uintptr_t)fi->pIPtoStateMap;
    int state = -1; unsigned best = 0;
    for (unsigned i = 0; i < fi->nIPMapEntries; ++i) {
        unsigned ip = m[i * 2];
        if (ip <= pc && ip >= best) { best = ip; state = (int)m[i * 2 + 1]; }
    }
    return state;
}

/* Run the cleanup (destructor) funclets for the states being exited, from
   `from` down to (but not including) `to`, per the UnwindMap. */
static void rxdk_frame_unwind(const FuncInfo *fi, int from, int to,
                              unsigned establisher) {
    const UnwindMapEntry *um = (const UnwindMapEntry *)(uintptr_t)fi->pUnwindMap;
    int s = from;
    for (int guard = 0; guard < 4096 && s != to && s >= 0; ++guard) {
        int next = um[s].toState;
        unsigned action = um[s].action;
        s = next;                                 /* advance first (re-entrant) */
        if (action) rxdk_eh_call_funclet(action, establisher);
    }
}

/*
 * The language handler the kernel dispatcher calls for each MSVC frame. This is
 * the standard two-pass MSVC/NT model, driven by the real kernel unwinder:
 *
 *   Pass 1 (no UNWINDING): find a catch in this frame whose type matches the
 *   thrown object. On a match, build the catch object, ask the kernel to unwind
 *   every inner frame down to this establisher (RtlUnwind2 -- runs their
 *   destructors through their handlers' UNWINDING pass), run this frame's own
 *   cleanup funclets to the try state, invoke the catch funclet, and resume the
 *   guest at the continuation IP it returns.
 *
 *   Pass 2 (UNWINDING): the kernel is unwinding THIS frame because an inner
 *   frame caught; run our cleanup funclets for the exited states.
 *
 * Establisher frame + CONTEXT come in as the 2nd/3rd args (kernel-supplied);
 * FuncInfo comes from dc->HandlerData. The whole algorithm -- establisher ==
 * the frame's incoming sp, ip2state at the call site, r12-based funclet frame,
 * catch object at establisher+dispCatchObj -- is the one proven end-to-end in
 * the RXDK xenia reference dispatcher (tests/eh: tc/tc2, destructor-before-catch
 * ordering verified). On-metal validation of the RtlUnwind2 handshake is the
 * remaining step; the non-throwing case (the vast majority of shipped libs)
 * resolves as ExceptionContinueSearch and needs none of this machinery.
 */
EXCEPTION_DISPOSITION __CxxFrameHandler(EXCEPTION_RECORD *rec, void *establisher,
                                        void *context, DISPATCHER_CONTEXT *dc) {
    const FuncInfo *fi = dc ? (const FuncInfo *)(uintptr_t)dc->HandlerData : 0;
    if (!fi || (fi->magicNumber & 0xFFFFFF00u) != 0x19930500u)
        return ExceptionContinueSearch;
    unsigned est = (unsigned)(uintptr_t)establisher;
    int cur = rxdk_state_from_ip(fi, (dc->ControlPc) - 1);

    if (rec->flags & EXCEPTION_UNWINDING) {
        /* second pass: run every cleanup funclet still live in this frame. */
        rxdk_frame_unwind(fi, cur, -1, est);
        return ExceptionContinueSearch;
    }

    /* first pass: find a matching catch in this frame's try blocks. */
    const ThrowInfo *ti = (const ThrowInfo *)(uintptr_t)rec->info[2];
    const TryBlockMapEntry *tb =
        (const TryBlockMapEntry *)(uintptr_t)fi->pTryBlockMap;
    for (unsigned t = 0; t < fi->nTryBlocks; ++t) {
        /* the try must enclose the current state */
        if (cur < tb[t].tryLow || cur > tb[t].tryHigh) continue;
        const HandlerType *ha =
            (const HandlerType *)(uintptr_t)tb[t].pHandlerArray;
        for (int h = 0; h < tb[t].nCatches; ++h) {
            if (!rxdk_type_matches(ha[h].pType, ti)) continue;

            /* build the catch object in the establisher frame */
            if (ha[h].dispCatchObj) {
                unsigned *slot = (unsigned *)(uintptr_t)(est + ha[h].dispCatchObj);
                *slot = rec->info[1];             /* thrown object ptr (by-value
                                                     int copied by the funclet) */
            }
            /* unwind all inner frames to this establisher (their destructors),
               then this frame's own cleanup to the try state. */
            RtlUnwind2(est, 0, rec, 0, context, 0);
            rxdk_frame_unwind(fi, cur, tb[t].tryLow, est);
            /* run the catch funclet; it returns where to resume past the try. */
            unsigned cont = rxdk_eh_call_funclet(ha[h].addressOfHandler, est);
            /* resume the guest at the continuation with this frame's context. */
            RtlUnwind2(est, cont, rec, 0, context, 0);
            abort();                              /* transfer does not return */
        }
    }
    return ExceptionContinueSearch;
}

/* __CxxFrameHandler3 alias (the 0x19930522 FuncInfo variant uses the same ABI). */
EXCEPTION_DISPOSITION __CxxFrameHandler3(EXCEPTION_RECORD *rec, void *establisher,
                                         void *context, DISPATCHER_CONTEXT *dc) {
    return __CxxFrameHandler(rec, establisher, context, dc);
}
