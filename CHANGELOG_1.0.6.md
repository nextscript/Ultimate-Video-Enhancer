# UVE Manager 1.0.6 – Changelog

Baseline: v1.0.5.

## New

- **AI Upscaling for AMD, Intel and macOS:** REAL-Video-Enhancer is no longer limited to the TensorRT backend
  (NVIDIA only). Settings → AI Upscaling now has a **Backend** choice:
  - **TensorRT** – NVIDIA RTX 20 or newer on Windows / Linux, as before (fastest on NVIDIA).
  - **NCNN (Vulkan)** – runs on every GPU with a Vulkan driver: AMD Radeon, Intel Arc / Iris Xe, NVIDIA, and on
    macOS Intel and Apple Silicon (MoltenVK). Small download (about 400 MB instead of 8–16 GB).
  - **PyTorch** – CUDA (NVIDIA), ROCm 6.4 (AMD, Linux), XPU (Intel Arc) or MPS (Apple Silicon).

  The recommended backend for the detected GPU is preselected (TensorRT on NVIDIA, NCNN otherwise). Packages and
  versions are the ones RVE itself installs for each backend (RVE `DownloadDeps.py` / `BuiltInTorchVersions.py`),
  and the portable Python is now downloaded for the actual system (Windows x64, Linux x64 / arm64, macOS x64 / arm64).
- Several backends can be installed side by side; **Use … for rendering** switches between installed ones without a
  new download.
- **NCNN models:** Nomos8k 4×, OpenProteus 2×, RealESR general 4×, AnimeJaNai V2 / V3 2×, Spanimation 2×,
  AniSD 2×, plus ESRGAN AnimeVideo V3 2× / 3× / 4× and ESRGAN Plus 4× (general and animation). Model ids are shared
  across backends, so an AI Upscale choice in the export settings survives a backend switch.
- Custom NCNN models (`.param` + `.bin`) can be imported; TensorRT / PyTorch still take `.pth` / `.safetensors`.

## Fixed

- An NCNN render no longer counts as failed when upscale-ncnn-py crashes while releasing the Vulkan device at exit
  (Windows 0xC0000005) – that happens after the video was written completely. The output file is still checked.

## Compatibility

- Existing 1.0.5 installations in `tools/rve` keep working unchanged: without a stored backend they are treated as
  TensorRT, and no packages are downloaded again.
- The render job log and the queue show the backend used (e.g. `hevc_nvenc + RVE NCNN (Vulkan)`).
- The self test runs the AI-upscale pipeline with the TensorRT and NCNN command lines (mock backend).
- `docs/LICENSES.md`: the RVE folder name was shown broken (`tools` + line break + `ve`); fixed.
