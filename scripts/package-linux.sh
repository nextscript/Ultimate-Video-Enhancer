#!/usr/bin/env bash
# UVE Player – Linux release: build, AppDir, AppImage (linuxdeploy + Qt plugin) and tar.gz
# Requires: Qt 6 (CMAKE_PREFIX_PATH or QT_ROOT_DIR), libmpv-dev, ninja, curl
#   bash scripts/package-linux.sh
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION=$(sed -n 's/^project(UVEPlayer VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
ARCH=$(uname -m)
OUT="$ROOT/release"
APPDIR="$OUT/AppDir"
BIN="$APPDIR/usr/bin"
echo "UVE Player $VERSION ($ARCH)"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ${QT_ROOT_DIR:+-DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"}
cmake --build build --parallel

rm -rf "$APPDIR"
mkdir -p "$BIN/plugins/uve" "$BIN/lang" "$BIN/tools/yt-dlp" "$BIN/docs" \
         "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/512x512/apps"
cp build/bin/UVEPlayer "$BIN/"
cp build/bin/plugins/*.so "$BIN/plugins/"
cp -r plugins/uve/data/* "$BIN/plugins/uve/"
cp build/bin/lang/*.qm lang/*.ts lang/README.md "$BIN/lang/"
cp docs/*.md "$BIN/docs/"
cp README.md CHANGELOG.md "$BIN/"

# yt-dlp (standalone Linux build) with checksum verification
# version from the redirect of releases/latest (the REST API is rate-limited on shared CI runners)
YTDLP_TAG=$(curl -fsSLI -o /dev/null -w '%{url_effective}' https://github.com/yt-dlp/yt-dlp/releases/latest | sed 's,.*/tag/,,')
[ -n "$YTDLP_TAG" ] || { echo "yt-dlp version not found"; exit 1; }
curl -fsSL -o "$BIN/tools/yt-dlp/yt-dlp" "https://github.com/yt-dlp/yt-dlp/releases/download/$YTDLP_TAG/yt-dlp_linux"
curl -fsSL -o /tmp/yt-dlp-sums "https://github.com/yt-dlp/yt-dlp/releases/download/$YTDLP_TAG/SHA2-256SUMS"
EXPECTED=$(awk '$2=="yt-dlp_linux"{print $1}' /tmp/yt-dlp-sums)
echo "$EXPECTED  $BIN/tools/yt-dlp/yt-dlp" | sha256sum -c -
chmod +x "$BIN/tools/yt-dlp/yt-dlp"
printf '{ "version": "%s", "source": "https://github.com/yt-dlp/yt-dlp/releases/tag/%s" }\n' "$YTDLP_TAG" "$YTDLP_TAG" > "$BIN/tools/yt-dlp/version.json"

cp app/resources/icon.png "$APPDIR/usr/share/icons/hicolor/512x512/apps/uve-player.png"
cat > "$APPDIR/usr/share/applications/uve-player.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=UVE Player
Comment=Media player with libmpv and Ultimate Video Enhancer
Exec=UVEPlayer %F
Icon=uve-player
Categories=AudioVideo;Video;Player;
MimeType=video/mp4;video/x-matroska;video/x-msvideo;video/webm;video/quicktime;audio/mpeg;audio/flac;application/x-mpegURL;
Terminal=false
EOF

# linuxdeploy + Qt plugin (AppImage tools need FUSE → extract-and-run mode)
TOOLS="$OUT/.tools"
mkdir -p "$TOOLS"
for t in linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage \
         linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$ARCH.AppImage; do
    f="$TOOLS/$(basename "$t")"
    [ -f "$f" ] || curl -fsSL -o "$f" "https://github.com/$t"
    chmod +x "$f"
done
export APPIMAGE_EXTRACT_AND_RUN=1
export QML_SOURCES_PATHS="$ROOT/app/qml:$ROOT/plugins/uve/qml:$ROOT/plugins/examples/sleeptimer"
export QMAKE="${QT_ROOT_DIR:+$QT_ROOT_DIR/bin/}qmake"
export LDAI_OUTPUT="$OUT/UVE-Player-$VERSION-linux-$ARCH.AppImage"
export OUTPUT="$LDAI_OUTPUT"
# The player plugins (usr/bin/plugins) only need Qt modules the executable already pulls in.
"$TOOLS/linuxdeploy-$ARCH.AppImage" --appdir "$APPDIR" \
    --executable "$BIN/UVEPlayer" \
    --desktop-file "$APPDIR/usr/share/applications/uve-player.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/512x512/apps/uve-player.png" \
    --plugin qt --output appimage

# tar.gz of the AppDir (for systems without FUSE); start via ./AppRun
tar -C "$OUT" -czf "$OUT/UVE-Player-$VERSION-linux-$ARCH.tar.gz" --transform "s,^AppDir,UVE-Player-$VERSION," AppDir
ls -la "$OUT"
