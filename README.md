# UVE Manager 1.0.2

Desktop video look editor and offline renderer built on the Ultimate Video Enhancer effect chain –
for Windows, macOS and Linux.

## Downloads

All packages are attached to the [GitHub release](https://github.com/nextscript/Ultimate-Video-Enhancer/releases/tag/1.0.2)
and built automatically by `.github/workflows/release.yml`:

| System | Package |
|---|---|
| Windows x64 | `UVE-Manager-1.0.2-setup.exe` (installer) or `UVE-Manager-1.0.2-portable.exe` (portable, ships FFmpeg) |
| macOS Apple Silicon | `UVE-Manager-1.0.2-mac-arm64.dmg` / `.zip` |
| macOS Intel | `UVE-Manager-1.0.2-mac-x64.dmg` / `.zip` |
| Linux x64 | `UVE-Manager-1.0.2-linux-x86_64.AppImage` / `UVE-Manager-1.0.2-linux-x64.tar.gz` |

- **macOS:** the app is not notarized – open it the first time with right click → *Open*
  (or `xattr -cr "/Applications/UVE Manager.app"`). FFmpeg must be installed, e.g. `brew install ffmpeg`.
- **Linux:** FFmpeg must be installed (`sudo apt install ffmpeg` or the package of your distribution).

## Keyboard shortcuts

| Action | Windows / Linux | macOS |
|---|---|---|
| Look toggles, HUD, Scopes, Auto-Scene-Match … | Ctrl+Alt+key | ⌃⌥key (Control+Option) |
| Undo / Redo | Ctrl+Z / Ctrl+Y | ⌘Z / ⇧⌘Z |
| Save / Open / Render | Ctrl+S / Ctrl+O / Ctrl+R | ⌘S / ⌘O / ⌘R |

All shortcuts can be changed in Settings → Keyboard shortcuts.

## Documentation

- `docs/README.md`: user and developer documentation
- `docs/BUILD.md`: build instructions per platform and the release workflow
- `CHANGELOG_1.0.2.md`: changes since 1.0.1
- `CHANGELOG_1.0.1.md`: changes since 1.0.0
- `migration/`: feature-parity inventory, parity comparison, capability probe, benchmark, migration/rollback notes
- `UVE_DESKTOP_V1.0.1_CROSS_PLATFORM_GPU_MIGRATION.md`: the specification of the cross-platform GPU release
