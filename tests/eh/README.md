# MSVC C++ exception interop test bed

`build.py` compiles a minimal MSVC-EH throw/catch with the **official** XDK
toolchain (cl.exe /EHsc -> link.exe with libcMT -> imagexex.exe), producing a
stock XEX that uses the real MS `__CxxFrameHandler` + `.pdata`/`.xdata`. It is the
decoupled test bed for the MSVC-EH interop work (see `docs/msvc-eh-interop.md`):
validating the xenia C++ EH dispatcher does not depend on the RXDK runtime side.

Requires the XDK (cl.exe) installed; not part of the normal `zig`/clang build or
the stdlib suite. Run: `python tests/eh/build.py` then run the XEX in xenia.

Expected once the xenia dispatcher (track 2) works:
    [T] before-throw
    [T] caught-int 1234      <-- catch reached (currently MISSING)
    [T] after-catch

Baseline in stock xenia-canary: `before-throw` and `after-catch` print, plus
"Guest attempted to throw a C++ exception!", but `caught-int` never prints --
RtlRaiseException logs the throw and returns without dispatching.
