# Build-time patches

Our changes to the pristine submodule trees under `vendor/` live here as tracked
patches and are applied at build time by `scripts/init-submodules.ps1`, never
committed into the submodules. This keeps the submodules at clean upstream commits
so bumping them is a re-pin plus, if needed, a patch refresh.

Layout: one subdirectory per submodule tree the patch targets.

```
patches/
  libcxx/      -> applied in vendor/llvm-project (the C++ runtime sources)
```

picolibc is **not** patched here any more: `vendor/picolibc` now points at the
`Team-Resurgent/picolibc` fork's `xbox360` branch, so its handful of Xbox 360
changes live as commits on that branch (the same model as the MS-PPC clang fork).
Updating upstream is a fetch + rebase of `xbox360`, then re-pin the submodule.

The apply step is **idempotent**: for each `*.patch` it first runs
`git apply --reverse --check`; if that succeeds the patch is already applied and is
skipped, otherwise it is applied with `git apply`. Re-running the init script is
therefore safe.

A patch is produced from a change made in the submodule working tree with
`git -C vendor/<tree> diff > patches/<tree>/NNNN-description.patch` (or
`git format-patch` for a commit). Keep them small and one concern each.

The MS-PPC clang patches are **not** here: they are commits on the
`xbox360-msppc` branch of the `vendor/llvm-project` fork, because they are large
and integral to the compiler. Only the runtime-library patches are build-time.
