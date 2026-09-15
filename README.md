# RXDK-360

<p align="center"><b>Xbox 360 development — currently Windows only, Visual Studio 2022 / 2026</b></p>

<p align="center">
  <a href="https://github.com/Team-Resurgent/RXDK-360/blob/main/vs20xx/extension/Rxdk360.Vsix/LICENSE.txt"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPL v3"></a>
  <a href="https://github.com/Team-Resurgent/RXDK-360/actions/workflows/release.yml"><img src="https://github.com/Team-Resurgent/RXDK-360/actions/workflows/release.yml/badge.svg" alt="Build"></a>
  <a href="https://discord.gg/VcdSfajQGK"><img src="https://img.shields.io/badge/chat-on%20discord-7289da.svg?logo=discord" alt="Discord"></a>
</p>

<p align="center">
  <a href="https://ko-fi.com/J3J7L5UMN"><img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi"></a>
  <a href="https://www.patreon.com/teamresurgent"><img src="https://img.shields.io/badge/Patreon-F96854?style=for-the-badge&logo=patreon&logoColor=white" alt="Patreon"></a>
</p>

<p align="center">
  <a href="https://github.com/Team-Resurgent/RXDK-360/releases/latest"><img src="https://img.shields.io/badge/download-latest-brightgreen.svg?style=for-the-badge&logo=github" alt="Download"></a>
</p>

A Visual Studio 2022 / 2026 toolchain for the Xbox 360. It reuses your own licensed
Xbox 360 XDK rather than redistributing Microsoft content. **Currently only Windows
with Visual Studio 2022 / 2026 is supported.**

## Getting started

1. Install a full Xbox 360 XDK (not a minimum install).
2. Download [RXDK-360-Setup.exe](https://github.com/Team-Resurgent/RXDK-360/releases/latest)
   and point the installer at your XDK setup EXE.
3. Open Visual Studio 2022 or 2026, create an Xbox 360 project, and build.

The installer lives side by side with a stock Microsoft Xbox 360 SDK.

## Related projects

- **[llvm-project](https://github.com/Team-Resurgent/llvm-project)** — Xbox 360 clang / lld
- **[XexTool](https://github.com/Team-Resurgent/XexTool)** — XEX packing
- **[RXDK-VS20XX](https://github.com/Team-Resurgent/RXDK-VS20XX)** — original Xbox Visual Studio extension

## License

GPLv3 — see [LICENSE.txt](vs20xx/extension/Rxdk360.Vsix/LICENSE.txt).
The Xbox 360 XDK is licensed Microsoft content and is **not** bundled.
