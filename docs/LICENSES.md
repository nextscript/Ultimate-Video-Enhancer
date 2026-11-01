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
  Manager: it is downloaded on request from its GitHub releases into `tools/rve` and run as a separate program
  (its source is available at the link above). Upscale models belong to their authors (see the RVE README / OpenModelDB).
  Depending on the backend chosen in Settings → AI Upscaling the same step installs PyTorch (BSD-style), NVIDIA TensorRT
  (NVIDIA license), ncnn by Tencent (BSD-3-Clause), upscale-ncnn-py / rife-ncnn-vulkan-python (MIT) and the portable
  CPython (PSF).
* **Video2X** by K4YT3X and contributors – GNU AGPL-3.0, https://github.com/k4yt3x/video2x (1.0.7). Not part of UVE
  Manager and not bundled in its packages (`tools/video2x` is excluded from the builds): it is downloaded on request from
  the official GitHub release 6.4.0 into `tools/video2x/<version>` (a `UVE-SOURCE.txt` there records the source, release
  and asset ids and the SHA-256) or the user selects an existing executable. It runs as a separate program; its source is
  available at the link above (tag 6.4.0). Its release contains FFmpeg libraries (LGPL/GPL), libplacebo (LGPL-2.1), ncnn
  (BSD-3-Clause) and Real-ESRGAN / Real-CUGAN / RIFE models under their own licenses. Bundling Video2X with UVE Manager
  would need a separate review of the AGPL obligations – a process boundary alone does not decide them.
* **Anime4K** by bloc97 – MIT, https://github.com/bloc97/Anime4K. The Anime4K v4 shaders ship with Video2X; UVE Manager
  does not copy them. Custom GLSL shaders imported by users keep their own licenses: record source, author and license in
  the shader library and keep the original license texts when redistributing shader packs.


## v1.0.1

- No new runtime dependencies. `@webgpu/types` (BSD-3-Clause) is used only at build time for type definitions.
- WebGPU is provided by Electron/Chromium (Dawn, BSD-3-Clause), which is already part of Electron's license
  bundle.
- The FFmpeg redistribution terms are unchanged: GPL build, downloaded by the user, not bundled in the
  installer unless `fetch-ffmpeg` is run before packaging.
