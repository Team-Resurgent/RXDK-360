# RXDK-360 hardware debugger — implementation plan

Goal: **source-level debugging of RXDK-360 titles on a real Xbox 360 devkit**, from
both VS2022 and VS Code, via a clean-room Debug Adapter Protocol (DAP) engine. No
Microsoft proprietary *debug engine* (the VS2010 AD7 package) is ported or shipped.

This follows the same DAP shape as the original-Xbox RXDK debugger (launch,
breakpoints, symbols on the host). The 360 transport is different (see next
section): the OG debugger reimplements the XBDM wire protocol in managed C#; for
the 360 we **P/Invoke the devkit's own `xbdm.dll`** instead.

## Transport decision: P/Invoke the 360 `xbdm.dll` (do NOT reimplement the wire)

The OG-Xbox debugger reimplements XBDM in managed code (`Rxdk.Xbdm.Managed`, port 731)
for cross-platform reach. Forking that for the 360 is the wrong move: the 360 XBDM
monitor has commands the original Xbox never had, so reconstructing the command set
from the OG client would silently **miss modern 360 commands**, and we'd have to get
the 360 connect/security handshake exactly right by guesswork.

Instead, the 360 XDK ships `xbdm.dll` — a standard Win32 DLL that exports the full
host debug API (**173 `Dm*` functions**: `DmOpenConnection`, `DmSetBreakpoint`,
`DmGetThreadContext`, `DmGo`/`DmStopOn`, `DmGetMemory`, `DmWalkLoadedModules`,
`DmRegisterNotificationProcessor`, `DmReboot`, ...), with clean `DMHRAPI` (`HRESULT
__stdcall`) signatures in `xbdm.h`. We already use this DLL for deploy (`xbcp`).
P/Invoking it means Microsoft's own client handles every command, the handshake,
framing and port (730) — so **it is impossible to miss a 360 command**, and there is
no wire-protocol to reverse-engineer.

Trade-offs, accepted: (a) **Windows-only** debugging — fine, since 360 devkit work
(the whole XDK) is Windows-centric anyway; (b) it depends on the user's **local**
`xbdm.dll` (proprietary XDK content — never shipped, exactly like `xbcp`/deploy today).
A cross-platform managed 360 client stays possible later, but it should be *derived by
capturing this `xbdm.dll`'s traffic* so it inherits the complete command set, not
guessed from the OG Xbox.

---

## Why not "port the VS2010 debugger"

The stock XDK's Xbox 360 debugger is a proprietary, VS2010-era native **AD7** engine
(a VSPackage bound to the old VS shell). We cannot ship or rehost it, it will not
load in VS2022, and it does nothing for VS Code. The `Xbox360Debugger` flavor we
register today (`debugger_remote.xml`) is only the property-page schema — there is
no engine behind it, which is why F5 on hardware gives "Unable to start debugging."
The correct, shippable path is a DAP engine of our own.

---

## Reference architecture (RXDK original-Xbox debugger)

Three stdio-connected processes plus a managed XBDM TCP client:

```
VS / VS Code ──DAP over stdio──► Rxdk.Dap (XboxDebugAdapter : DebugAdapterBase)
                                     │ line-delimited JSON over child stdin/stdout
                                     ▼
                                xboxdbg-bridge (DebugBridgeSession)  ── uses ──► Rxdk.Xbox360.Pdb (symbols)
                                     │ in-process
                                     ▼
                                Rxdk.Xbdm.Managed (IXbdmDebugConnection)
                                     │ TCP :731, ASCII "COMMAND ARG=..\r\n"
                                     ▼
                                XBDM debug monitor on the devkit
```

- **DAP layer** uses `Microsoft.VisualStudio.Shared.VSCodeDebugProtocol`; the adapter
  subclasses `DebugAdapterBase`, runs on stdio, defers launch until
  `configurationDone`, and handles setBreakpoints / stackTrace / scopes / variables /
  continue / next / stepIn / stepOut / threads / evaluate.
- **Bridge** is the orchestration brain: launch/attach/go/step state machine,
  breakpoint bookkeeping (`BreakpointManager`), notification fan-out, module-base
  relocation (`SymbolService`), value formatting (`ManagedSymbols`).
- **XBDM transport is fully managed** — the wire protocol is reimplemented in C#
  (`Rxdk.Xbdm.Managed`), NOT `xbdm.dll` P/Invoke. Port is `XbdmConstants.DebuggerPort
  = 0x2db (731)`. Commands: `DEBUGGER CONNECT`, `BREAK ADDR=/START/WRITE=`, `GO`/
  `STOP`, `CONTINUE THREAD=`, `GETMEM ADDR= LENGTH=`, context get/set, `GetThreadList`,
  `SetTitle`, `REBOOT`. Async notifications (`DmBreak/DmSingleStep/DmModLoad/
  DmException/DmDebugStr/...`) arrive on a persistent notification channel.
- **Symbols** = a from-scratch managed **PDB/CodeView** reader (`Rxdk.Xbox360.Pdb`:
  MSF/TPI/DBI). `PdbImage` exposes: source+line→RVA (`TryResolveLine`), RVA→source
  (`TryFindLine`/`TryFindFunctionName`), frame locals (`FindFrame` → `LocalVariable`
  with `FrameOffset`/`FrameBase`), and a type system for visualizers. Handles both
  LLVM (`S_LOCAL`+`S_DEFRANGE_*`) and legacy (`S_BPREL32`/`S_REGREL32`) locals.

### Reusable as-is (target-agnostic)
- `Rxdk.Dap` DAP layer and the DAP↔bridge line-JSON framing.
- The bridge orchestration (launch/attach/go/step machine, notification fan-out,
  pending-breakpoint queue, relocation logic).
- The MSF/TPI/DBI PDB machinery (PDB is container format, ISA-independent).

### Must be re-authored for PowerPC / 360 (the clean seams)
1. `IXbdmDebugConnection` + `Rxdk.Xbdm.Managed` → a **360 XBDM** client.
2. `XbdmContext` + `EmitRegisters` / `ResolveLocalAddress` / `GetStack` → **PPC**
   register set + unwinder.
3. The symbol reader behind `SymbolService`/`ManagedSymbols` → **DWARF** (modern)
  while keeping `Rxdk.Xbox360.Pdb` for the legacy toolset.

---

## 360 deltas (what actually changes)

| Area | Original Xbox | Xbox 360 |
|---|---|---|
| **XBDM port** | **731** (`0x2db`) | **730** (`0x2da`) — hardcoded port + whole dialect differ |
| Wire dialect / handshake | OG XBDM text set | 360 XBDM command set + connect/security handshake (reverse-engineer / community-documented) |
| Endianness | little | **big-endian** contexts/memory |
| CPU context | x86 `CONTEXT` (Edi..Esp, EFlags, Dr0-7, i387) | PPC: **GPR0-31, LR, CTR, CR, XER, MSR, FPR0-31, VMX/VSCR** |
| Stack unwind | EBP walk (`[ebp]`, `[ebp+4]`) | **PPC back-chain**: r1 → saved SP at `[r1]`, LR at the caller's LR-save slot |
| Single-step | x86 trap flag `EFlags|=0x100` | **MSR[SE]** single-step bit (or the 360 XBDM step facility) |
| Address space | base `0x400000`, kit range checks 0x00400000-0x00600000 | XEX base **0x82000000** (default), 360 memory map / kit-kernel ranges |
| Image + symbols | XBE + PDB | **XEX + DWARF** (modern clang, `.elf` kept beside the XEX via `KeepElf`); **XEX + PDB** (legacy 2010-01) |
| Launch | SetTitle + reboot | deploy via `xbcp` (already wired) + `SetTitle`/`REBOOT` to boot `default.xex` |
| CodeView reg ids (legacy PDB) | x86 (`CvRegEbp`=22, VFRAME) | PPC CodeView register numbering + frame convention |

---

## Symbol strategy

Two toolsets, two symbol sources, one interface (`SymbolService`/`ManagedSymbols`
stay; the reader behind them swaps):

- **Modern (clang)** — primary. The link already keeps the **DWARF-carrying `.elf`**
  next to the XEX (`ClangLink.KeepElf` in Debug). Need a **managed DWARF reader**
  (`.debug_line` for line↔address, `.debug_info`/`.debug_loc`(list) for locals and
  types). This is new code but replaces `Rxdk.Xbox360.Pdb` behind the same interface.
  Caveat to verify early: DWARF completeness for locals — Debug compiles `-O0
  -gdwarf-4`, but confirm variable location lists are emitted (past issue: line-tables
  -only stripped locals).
- **Legacy (2010-01)** — the stock toolset emits a **PDB**; `Rxdk.Xbox360.Pdb` reads it.
  Only the PPC CodeView register numbers in `S_FRAMEPROC`/`S_REGREL` decoding differ.

---

## Milestones (de-risk the unknowns first)

- **M0 — 360 XBDM transport spike (highest risk).** Managed `TcpClient` to
  `console:730`, read the welcome/status line, complete the connect/security
  handshake, issue a trivial command (e.g. drive info / `magicboot`-less status),
  disconnect. Proves we can speak 360 XBDM at all before building anything on it.
- **M1 — connect + launch + stop at entry.** Deploy the XEX (`xbcp`), `SetTitle` +
  `REBOOT` to boot it, attach the debugger, arm the initial breakpoint, catch the
  first stop notification. Proves the full run loop.
- **M2 — first source breakpoint hit.** Resolve a source line → address via DWARF,
  set the breakpoint, run, hit it, surface it in the IDE. The headline milestone.
- **M3 — stack + registers.** Read the PPC context, walk the back-chain, map each
  frame address→source, show registers.
- **M4 — locals + watch.** DWARF variable locations relative to the frame; format
  scalars/pointers/aggregates (carry over the STL visualizers if the layout matches).
- **M5 — stepping.** MSR[SE] single-step; step over/in/out.
- **M6 — IDE wiring.** VS Code `package.json` debug-type contribution + `launch.json`
  schema; VS Debug Adapter Host registration in the VSIX. Flip the platform's F5 for
  a HW-deploy config from the dead `Xbox360Debugger` flavor to this adapter.

Parallel, independent quick win (not on this critical path): a **run-on-HW without
debugging** override (Ctrl+F5 = `xbcp` deploy + `REBOOT`-to-title), mirroring the
Xenia local-debugger trick. Gives "build → run on the devkit" immediately while the
full engine is built.

---

## New vs reused (component ledger)

- **New:** `Rxdk.Xbdm360.Managed` (360 wire client, port 730, PPC context); a managed
  **DWARF reader**; PPC unwinder + register/step logic in the bridge; the IDE debug
  contributions.
- **Fork from OG-Xbox debugger:** the bridge orchestration and `SymbolService`/
  `ManagedSymbols` (swap register/frame specifics + symbol backend).
- **Reuse as-is:** the DAP server, the DAP↔bridge protocol, the PDB machinery (for the
  legacy toolset).

## First concrete step

Build the **M0 spike** as a throwaway console app: managed TCP to a devkit on 730 +
handshake + one status command. It needs a **live devkit** to validate, and it tells
us whether the 360 XBDM dialect is close enough to the OG one to fork, or different
enough to treat as its own client. Everything else depends on that answer.
