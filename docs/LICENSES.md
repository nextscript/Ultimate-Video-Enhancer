# Licenses and attribution

* **Ultimate Video Enhancer** (Sharpen, HDR, Color Boost) – userscript by Freak288 / nextscript,
  https://github.com/nextscript/Ultimate-Video-Enhancer. UVE Manager ports its effect algorithms, constants, presets
  and file formats and downloads the repository's `LUTsProfiles_v3.0.zip`. The repository contains no LICENSE file
  (checked at commit 38b2ed1); clarify redistribution terms with the author before publishing UVE Manager builds.
* **FFmpeg** – © the FFmpeg developers, LGPL/GPL. The portable build (BtbN FFmpeg-Builds, `win64-gpl`) is GPL-3.0
  because it includes x264/x265 and other GPL components; its `LICENSE.txt` is placed in `tools\ffmpeg`. Distributing
  installers that bundle it requires complying with the GPL (source offer for the FFmpeg build).
* **Electron** (MIT), **Chromium** (BSD-style + third-party notices shipped with Electron), **React** (MIT),
  **fflate** (MIT), **Vite**, **esbuild**, **TypeScript**, **electron-builder** (MIT / Apache-2.0, build-time only).
* Algorithms implemented from public specifications: SVG 1.1 / Filter Effects (feTurbulence reference code,
  compositing and blending formulas). No third-party shader code is bundled.
* **REAL-Video-Enhancer** by TNTwise – GNU AGPL-3.0, https://github.com/TNTwise/REAL-Video-Enhancer. Not part of UVE
  Manager: it is downloaded on request from its GitHub releases into `toolsve` and run as a separate program
  (its source is available at the link above). Upscale models belong to their authors (see the RVE README / OpenModelDB).
  PyTorch (BSD-style), NVIDIA TensorRT (NVIDIA license) and the portable CPython (PSF) are installed by the same step.


## v1.0.1

- No new runtime dependencies. `@webgpu/types` (BSD-3-Clause) is used only at build time for type definitions.
- WebGPU is provided by Electron/Chromium (Dawn, BSD-3-Clause), which is already part of Electron's license
  bundle.
- The FFmpeg redistribution terms are unchanged: GPL build, downloaded by the user, not bundled in the
  installer unless `fetch-ffmpeg` is run before packaging.
