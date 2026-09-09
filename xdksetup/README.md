# XDK setup

Put the Xbox 360 XDK setup installer here, e.g.:

    xdksetup/XDKSetupXenon<version>.exe

The installer itself is **not** committed (`*.exe` is gitignored) — it is large
and license-restricted. Drop your own copy in this folder.

A setup script (planned) will extract only the pieces the toolchain needs
(headers, import libraries, etc.) from the installer, so nothing from the XDK is
checked into the repository.
