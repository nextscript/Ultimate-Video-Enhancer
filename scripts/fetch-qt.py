"""Lädt Qt 6 (MSVC 2022 x64) direkt aus dem offiziellen Qt-Online-Repository und entpackt es.

Hintergrund: Qt hat das Repository-Layout auf Unterordner pro Architektur umgestellt
(…/qt6_6120/qt6_6120_msvc2022_64/Updates.xml); aqtinstall 3.3.0 unterstützt das nicht.

Aufruf:  python -I scripts/fetch-qt.py <version z. B. 6.12.0> <zielordner z. B. M:/LAPS/Qt>
Jedes Archiv wird gegen die vom Server gelieferte SHA-1-Summe geprüft.
"""
import hashlib
import os
import subprocess
import sys
import urllib.request
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEVENZ = os.path.join(ROOT, "deps", "tools", "7zr.exe")
ARCH = "win64_msvc2022_64"
# Nur benötigte Archive des Basispakets (Doku/Beispiele werden ausgelassen)
SKIP = ("qtdoc", "qtexamples")
ADDONS = ["qtshadertools", "qtimageformats"]


def fetch(url):
    with urllib.request.urlopen(url, timeout=120) as r:
        return r.read()


def main():
    version = sys.argv[1] if len(sys.argv) > 1 else "6.12.0"
    target = sys.argv[2] if len(sys.argv) > 2 else "M:/LAPS/Qt"
    vtag = version.replace(".", "")
    base = f"https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_{vtag}/qt6_{vtag}_msvc2022_64"
    root = ET.fromstring(fetch(base + "/Updates.xml"))
    wanted = {f"qt.qt6.{vtag}.{ARCH}"} | {f"qt.qt6.{vtag}.addons.{a}.{ARCH}" for a in ADDONS}
    cache = os.path.join(ROOT, "deps", "qt-archives")
    os.makedirs(cache, exist_ok=True)
    # Neuere Archive enthalten keinen "<version>/msvc2022_64"-Präfix mehr → direkt in den Zielordner entpacken
    qtdir = os.path.join(target, version, "msvc2022_64")
    os.makedirs(qtdir, exist_ok=True)
    count = 0
    for p in root.findall("PackageUpdate"):
        name = p.findtext("Name")
        if name not in wanted:
            continue
        ver = p.findtext("Version")
        archives = [a.strip() for a in (p.findtext("DownloadableArchives") or "").split(",") if a.strip()]
        for a in archives:
            if a.startswith(SKIP):
                continue
            url = f"{base}/{name}/{ver}{a}"
            dest = os.path.join(cache, a)
            expected = fetch(url + ".sha1").decode().split()[0].strip().lower()
            if not (os.path.exists(dest) and hashlib.sha1(open(dest, "rb").read()).hexdigest() == expected):
                print("lade", a, flush=True)
                data = fetch(url)
                got = hashlib.sha1(data).hexdigest()
                if got != expected:
                    sys.exit(f"Prüfsumme falsch: {a} ({got} != {expected})")
                open(dest, "wb").write(data)
            print("entpacke", a, flush=True)
            subprocess.run([SEVENZ, "x", "-y", "-bso0", "-bsp0", f"-o{qtdir}", dest], check=True)
            count += 1
    # qt.conf wie bei aqtinstall: Prefix relativ zum bin-Ordner, damit Tools (windeployqt, qtpaths) den Ort finden
    with open(os.path.join(qtdir, "bin", "qt.conf"), "w", encoding="utf-8") as f:
        f.write("[Paths]\nPrefix=..\n")
    print(f"fertig: {count} Archive nach {qtdir}")


if __name__ == "__main__":
    main()
