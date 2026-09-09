# Pending stdlib sections

Tests here are for standard features not yet fully brought up. They are kept out
of the globbed suite (tools/run_stdlib_tests.py only scans tests/stdlib/*.c[pp])
so the suite stays green for what works.

- **t_format.cpp** — `<std::format>` (C++20/23). Needs libc++'s `charconv.cpp`,
  whose float `from_chars` pulls llvm-libc's shared `FPBits.h`; that expects a
  `_LIBCPP_VERBOSE_ABORT` integration this libc++ snapshot doesn't wire up
  cleanly for our out-of-tree build. Integer `to_chars` alone would need
  separating from the float path. Revisit with the llvm-libc shared-header setup.
