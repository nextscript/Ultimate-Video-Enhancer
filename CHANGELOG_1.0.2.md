# UVE Manager 1.0.2 – Changelog

Baseline: v1.0.1.

## Fixed

- **Export failed with `Decoder failed (…) Invalid argument … Nothing was written into output file`** on
  Windows 11 laptops (Intel QSV / D3D11VA hardware decoding).
  - The per-file hardware decoding probe now runs the real decode filter chain, so a hardware decoder whose
    frames cannot be scaled/converted is no longer selected.
  - If a hardware decoder still fails before delivering the first frame, the export retries once with
    software decoding (logged as `hardware decoding failed … retrying with software decoding`).

## New

- **macOS and Linux release builds.** A GitHub Actions workflow (`.github/workflows/release.yml`) builds on
  every version tag:
  - Windows: setup + portable exe
  - macOS: dmg + zip for Intel (x64) and Apple Silicon (arm64)
  - Linux: AppImage + tar.gz
  It attaches the packages and `SHA256SUMS.txt` to the GitHub release.
- **macOS keyboard shortcuts:**
  - Editor commands use Command: ⌘Z / ⇧⌘Z / ⌘S / ⌘O / ⌘R.
  - The userscript toggles use Control+Option (⌃⌥key), because ⌘⌥H / ⌘⌥D are taken by macOS.
  - Keys are matched by the physical key when Option (or AltGr on Windows) changes the character.
  - All labels show ⌃⌥⇧⌘.
  - A minimal app menu enables ⌘Q / ⌘H / ⌘M and cut/copy/paste in text fields.
- **FFmpeg lookup** also searches the Homebrew, MacPorts, Linuxbrew and Snap folders. Apps started from Finder
  or a desktop launcher do not get the shell `PATH`.
- **Updates:**
  - The release source is now `nextscript/Ultimate-Video-Enhancer`; the old default is migrated.
  - macOS and Linux pick the dmg / AppImage of their architecture.

## Build

- `build-portable.mjs` finds `makensis.exe` in any electron-builder cache layout, with a system NSIS as fallback.
- All `dist*` scripts pass `--publish never`; `dist:mac` builds x64 and arm64.
