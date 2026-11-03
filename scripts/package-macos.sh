#!/usr/bin/env bash
# UVE Player – macOS release: build, app bundle (macdeployqt) and DMG
# Requires: Qt 6 (QT_ROOT_DIR or CMAKE_PREFIX_PATH), Homebrew mpv + pkg-config, ninja
#   bash scripts/package-macos.sh
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION=$(sed -n 's/^project(UVEPlayer VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
ARCH=$(uname -m)
OUT="$ROOT/release"
echo "UVE Player $VERSION (macOS $ARCH)"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ${QT_ROOT_DIR:+-DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"}
cmake --build build --parallel

APP="build/bin/UVEPlayer.app"
M="$APP/Contents/MacOS"
mkdir -p "$M/plugins/uve" "$M/lang" "$M/tools/yt-dlp" "$M/docs"
cp build/bin/plugins/*.dylib "$M/plugins/"
cp -R plugins/uve/data/* "$M/plugins/uve/"
cp build/bin/lang/*.qm lang/*.ts lang/README.md "$M/lang/"
cp docs/*.md "$M/docs/"
cp README.md CHANGELOG.md "$M/"
cp app/resources/icon.png "$APP/Contents/Resources/" 2>/dev/null || true

# yt-dlp (standalone macOS build) with checksum verification
# version from the redirect of releases/latest (the REST API is rate-limited on shared CI runners)
YTDLP_TAG=$(curl -fsSLI -o /dev/null -w '%{url_effective}' https://github.com/yt-dlp/yt-dlp/releases/latest | sed 's,.*/tag/,,')
[ -n "$YTDLP_TAG" ] || { echo "yt-dlp version not found"; exit 1; }
curl -fsSL -o "$M/tools/yt-dlp/yt-dlp" "https://github.com/yt-dlp/yt-dlp/releases/download/$YTDLP_TAG/yt-dlp_macos"
curl -fsSL -o /tmp/yt-dlp-sums "https://github.com/yt-dlp/yt-dlp/releases/download/$YTDLP_TAG/SHA2-256SUMS"
EXPECTED=$(awk '$2=="yt-dlp_macos"{print $1}' /tmp/yt-dlp-sums)
echo "$EXPECTED  $M/tools/yt-dlp/yt-dlp" | shasum -a 256 -c -
chmod +x "$M/tools/yt-dlp/yt-dlp"
printf '{ "version": "%s", "source": "https://github.com/yt-dlp/yt-dlp/releases/tag/%s" }\n' "$YTDLP_TAG" "$YTDLP_TAG" > "$M/tools/yt-dlp/version.json"

# Bundle Qt, libmpv (Homebrew) and their dependencies; the player plugins are deployed as extra executables
DEPLOY="${QT_ROOT_DIR:+$QT_ROOT_DIR/bin/}macdeployqt"
EXTRA=()
for p in "$M"/plugins/*.dylib; do EXTRA+=("-executable=$p"); done
"$DEPLOY" "$APP" -qmldir="$ROOT/app/qml" -qmldir="$ROOT/plugins/uve/qml" -qmldir="$ROOT/plugins/examples/sleeptimer" \
    "${EXTRA[@]}" -always-overwrite
# ad-hoc signature (required to start on Apple Silicon); not notarized
codesign --force --deep --sign - "$APP"

DMGDIR="$OUT/dmg"
rm -rf "$DMGDIR"; mkdir -p "$DMGDIR"
cp -R "$APP" "$DMGDIR/UVE Player.app"
ln -s /Applications "$DMGDIR/Applications"
# hdiutil fails now and then with "Resource busy" on CI runners (background services hold the image) → retry
DMG="$OUT/UVE-Player-$VERSION-macos-$ARCH.dmg"
for attempt in 1 2 3 4 5; do
    if hdiutil create -volname "UVE Player $VERSION" -srcfolder "$DMGDIR" -ov -format UDZO "$DMG"; then break; fi
    if [ "$attempt" -eq 5 ]; then echo "hdiutil failed 5 times"; exit 1; fi
    echo "hdiutil failed (attempt $attempt), retrying in $((attempt * 10)) s ..."
    sleep $((attempt * 10))
done
rm -rf "$DMGDIR"
ls -la "$OUT"
