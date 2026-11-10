#!/usr/bin/env bash
# Synthetische DVD-Video-Testdisc für tst_optical_playback (keine echten Filme, keine Fremddaten).
#   tests/fixtures/make-dvd-fixture.sh <ausgabeordner>
# Ergebnis: <ausgabe>/DVD (VIDEO_TS-Ordner) und <ausgabe>/uve-test-dvd.iso
#   - VMGM-Menü mit zwei Knöpfen (beide starten Titel 1; Kapitelsprünge aus dem VMGM erlaubt DVD-Video nicht), Start im Menü (First Play → Menü)
#   - Titel 1: 12 s Testbild (PAL, MPEG-2) mit AC-3-Ton, Kapitel bei 0:00 und 0:06
# Benötigt (Dev-Abhängigkeiten, nur für Tests): ffmpeg, dvdauthor + spumux, genisoimage (oder xorriso -as mkisofs).
# Ohne Root-Rechte unter Ubuntu: apt-get download dvdauthor libdvdread8t64 genisoimage; dpkg -x <deb> root;
# dann PATH/LD_LIBRARY_PATH auf root/usr/bin bzw. root/usr/lib/x86_64-linux-gnu setzen.
set -euo pipefail
out="${1:?Ausgabeordner fehlt}"
mkdir -p "$out"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cd "$work"

ff() { ffmpeg -hide_banner -loglevel error -y "$@"; }

# Titel 1: Testbild mit Zeitstempelmuster, 1-kHz-Ton
ff -f lavfi -i testsrc=size=720x576:rate=25:duration=12 -f lavfi -i sine=frequency=1000:sample_rate=48000:duration=12 \
   -target pal-dvd -aspect 16:9 -c:a ac3 -b:a 192k title1.mpg

# Menü-Hintergrund (3 s, wird mit pause="inf" als Standbild gehalten)
ff -f lavfi -i color=c=0x203050:size=720x576:rate=25:duration=3 -f lavfi -i anullsrc=r=48000:cl=stereo \
   -t 3 -target pal-dvd -aspect 16:9 -c:a ac3 -b:a 192k menu.mpg

# Knopfgrafiken (höchstens 4 Farben, Transparenz) für spumux
box() { ff -f lavfi -i "color=c=black@0.0:size=720x576,format=rgba" -frames:v 1 \
        -vf "drawbox=x=100:y=200:w=220:h=80:color=$1@1.0:t=fill,drawbox=x=400:y=200:w=220:h=80:color=$1@1.0:t=fill" "$2"; }
box yellow highlight.png
box red select.png
cat > spumux.xml <<'XML'
<subpictures>
  <stream>
    <spu force="yes" start="00:00:00.00" highlight="highlight.png" select="select.png">
      <button name="b1" x0="100" y0="200" x1="320" y1="280" right="b2"/>
      <button name="b2" x0="400" y0="200" x1="620" y1="280" left="b1"/>
    </spu>
  </stream>
</subpictures>
XML
spumux -m dvd spumux.xml < menu.mpg > menu_sp.mpg 2> spumux.log

cat > dvd.xml <<'XML'
<dvdauthor>
  <vmgm>
    <fpc>jump vmgm menu 1;</fpc>
    <menus>
      <video format="pal" aspect="16:9"/>
      <pgc entry="title">
        <vob file="menu_sp.mpg" pause="inf"/>
        <button name="b1">jump title 1;</button>
        <button name="b2">jump title 1;</button>
      </pgc>
    </menus>
  </vmgm>
  <titleset>
    <titles>
      <video format="pal" aspect="16:9"/>
      <pgc>
        <vob file="title1.mpg" chapters="0,0:06"/>
        <post>call vmgm menu 1;</post>
      </pgc>
    </titles>
  </titleset>
</dvdauthor>
XML
rm -rf "$out/DVD"
VIDEO_FORMAT=PAL dvdauthor -o "$out/DVD" -x dvd.xml 2> dvdauthor.log

if command -v genisoimage >/dev/null; then
    genisoimage -quiet -dvd-video -V UVE_TEST_DVD -o "$out/uve-test-dvd.iso" "$out/DVD"
elif command -v xorriso >/dev/null; then
    xorriso -as mkisofs -quiet -dvd-video -V UVE_TEST_DVD -o "$out/uve-test-dvd.iso" "$out/DVD"
fi
ls -l "$out/DVD/VIDEO_TS" "$out"/*.iso 2>/dev/null || true
