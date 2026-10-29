# Building UVE Manager 1.0.2

Common requirements: Node.js 22+ and npm. `npm install` once.

```text
npm run typecheck   # TypeScript
npm test            # unit tests (30)
npm run selftest    # end-to-end renders incl. WebGPU vs WebGL2 parity (needs FFmpeg)
npm start           # run from source (data in <project>\data)
```

## Release workflow (GitHub Actions)

`.github/workflows/release.yml` builds Windows, macOS and Linux and attaches the packages to the release:

1. Set the version in `package.json` (plus `package-lock.json` and `APP_VERSION` in `shared/types.ts`).
2. Push a tag with the same version: `git tag 1.0.2 && git push origin 1.0.2`.
   Alternatively, start *Actions → Release builds → Run workflow* with the tag. This needs the workflow on the
   default branch.

The workflow fails when the tag does not match the version. macOS builds are ad-hoc signed unless the secrets
`MAC_CERT_P12_BASE64`, `MAC_CERT_PASSWORD`, `APPLE_ID`, `APPLE_APP_SPECIFIC_PASSWORD` and `APPLE_TEAM_ID` are set.
With those secrets they are signed with a Developer ID and notarized.

## Windows (tested: Windows 10 21H2 LTSC x64, NVIDIA RTX 5060 Ti)

```text
npm run dist:portable   # release\UVE-Manager-1.0.2-portable.exe (self-extractor, asks for the target folder)
npm run dist            # + NSIS installer release\UVE-Manager-1.0.2-setup.exe
```

The NSIS compiler from electron-builder's cache (or a system NSIS) is used for the self-extractor. FFmpeg is downloaded by the
app on first start into `tools\ffmpeg` (or `npm run fetch-ffmpeg` before packaging to ship it).

GPU paths:

| Path | Status |
|---|---|
| WebGPU via D3D12 | tested |
| NVENC/NVDEC (cuda) | tested |
| AMD AMF/D3D11VA | untested; candidates are verified at runtime |
| Intel QSV | untested; candidates are verified at runtime |

## macOS – UNTESTED

```text
npm run dist:mac        # dmg + zip for x64 and arm64
```

- Requires building on macOS.
- Signing/notarisation are not configured: set `CSC_LINK`/`CSC_KEY_PASSWORD` and the notarisation
  credentials of electron-builder for distribution.
- FFmpeg must be installed (e.g. Homebrew) or set in Settings → Tools.
- **GPU paths:** WebGPU via Metal; encoding with VideoToolbox, verified by a scratch encode.
- **Not available:** AI upscaling (TensorRT).

## Linux – UNTESTED

```text
npm run dist:linux      # AppImage + tar.gz
```

- Needs a system FFmpeg with the wanted encoders (`ffmpeg -encoders`).
- **Encoding:** VA-API needs `/dev/dri/renderD128` and the Mesa/Intel media driver; NVENC needs the
  proprietary NVIDIA driver.
- **WebGPU:** Electron/Chromium may need Vulkan (`--enable-features=Vulkan`) depending on the driver. If WebGPU
  is unavailable or fails validation, the app falls back to WebGL2 automatically.
- **Headless/remote sessions:** only the software paths are expected to work.

## Hardware evidence

Run `electron . --probe-capabilities=migration/capabilities/<host>.json` on each test machine. The result
lists the OS, GPU, driver, FFmpeg build, verified hardware encoders/decoders and the WebGPU validation result.
Only platforms with such a file and a passing selftest should be called supported.
