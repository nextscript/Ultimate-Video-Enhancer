# UVE Player - Third-Party Components and Licenses

This overview was compiled with care but is NOT legal advice. Before any public or
commercial distribution, the exact license terms of the builds used must be reviewed.

By installing or using UVE Player you accept the license terms of the components listed below.


## Qt 6.12.0
- License: LGPL v3 (or commercial)
- Usage: dynamically linked DLLs (Core, Gui, Quick, QuickControls2, Qml, Network, OpenGL, ...)
- Notes: the Qt DLLs can be replaced by the user (dynamic linking).
  Source code: https://www.qt.io/download-open-source

## mpv / libmpv  v0.41.0-1386-g2137d21b2 (shinchiro build 20261102)
- License: GPL v2+ or LGPL v2.1+, depending on the build configuration
- Usage: mpv\libmpv-2.dll
- Notes: the build includes FFmpeg, libplacebo, libass, libbluray and others. The actual license
  of the build (GPL components) has to be verified. Under the GPL, the source code must be made
  available for the complete distribution.
  Sources: https://github.com/mpv-player/mpv, https://github.com/shinchiro/mpv-winbuild-cmake

## libdvdnav / libdvdread / libbluray (included in libmpv, 1.1.8)
- License: GPL v2+ (libdvdnav, libdvdread), LGPL v2.1+ (libbluray)
- Usage: inside mpv\libmpv-2.dll of the shinchiro build (`mpv-configuration`: `-Ddvdnav=enabled -Dlibbluray=enabled`); no separate files
- Not included: libaacs, libbdplus (no decryption), the libbluray BD-J jar and a Java runtime, libcdio (audio CDs are read by UVE itself)
- Sources: https://code.videolan.org/videolan/libdvdnav, https://code.videolan.org/videolan/libdvdread, https://code.videolan.org/videolan/libbluray

## FFmpeg (included in libmpv)
- License: LGPL / GPL, depending on the build
- Usage: via libmpv (see above)

## yt-dlp 2026.10.26
- License: Unlicense
- Usage: tools\yt-dlp\yt-dlp.exe (standalone program)
- Source: https://github.com/yt-dlp/yt-dlp

## 7-Zip (7zr.exe) 26.05
- License: LGPL v2.1 (the BSD-3 / unRAR restrictions do not apply to 7zr)
- Usage: tools\7zr.exe (standalone, used by the tool updater)
- Source: https://www.7-zip.org

## Ultimate Video Enhancer 1.17.5 (userscript)
- License: no license statement found in the repository
- Usage: formulas and parameters ported natively; bundled data: NaturalBoost.cube, example profiles
- Rights holder: Freak288 / nextscript - used in the rights holder's own project

## NSIS 3.0.4.1 (build only)
- License: zlib/libpng
- Usage: creates the installer; not part of the program


## Privacy
The player contains no telemetry. Network access happens only for streams you open and for the
update check (GitHub API), which runs only when enabled in the settings.
