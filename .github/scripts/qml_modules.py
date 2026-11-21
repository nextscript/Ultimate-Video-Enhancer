#!/usr/bin/env python3
"""QML-Erweiterungen reiner QML-Plugins für Linux und macOS bauen (.github/workflows/build-qml-modules.yml).

Ein QML-Plugin ("main" im Manifest) kann eigene QML-Erweiterungen mitbringen, z. B. oauth/uve_oauth.dll mit einer
qmldir ("plugin uve_oauth"). Qt lädt daraus je System uve_oauth.dll, libuve_oauth.so oder libuve_oauth.dylib – alle
drei liegen nebeneinander im selben Ordner, das Manifest bleibt unverändert.

Quellen: sources/<Name>/ im Katalog-Zweig mit CMakeLists.txt und uve-build.json:
    {"qmlModules": [{"target": "uve_oauth", "dir": "oauth", "check": "OAuthLoopback {}"}]}

Befehle:
    resolve   --zip-file X.zip --catalog <Pfad zum ausgecheckten Katalog-Zweig> [--force]
    arch      --module <Datei> --key linux-x86_64|macos-arm64
    checkqml  --dir <Ordner mit qmldir und Modul> --check "<QML-Objekt>" --out test.qml
    assemble  --base <hochgeladenes ZIP> --parts <Ordner mit <key>/<dir>/<datei>> --modules <JSON> --out <Ordner>
"""
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

CONFIG = Path(__file__).resolve().parent.parent / "plugin-builds.json"
TARGETS = ["linux-x86_64", "macos-arm64"]          # Windows-Modul kommt aus dem hochgeladenen ZIP
FILE_OF = {"windows-x64": "{t}.dll", "linux-x86_64": "lib{t}.so", "macos-arm64": "lib{t}.dylib"}
RUNNERS = {"linux-x86_64": "ubuntu-24.04", "macos-arm64": "macos-latest"}


def fail(msg):
    print(f"::error::{msg}")
    sys.exit(1)


def out(**values):
    path = os.environ.get("GITHUB_OUTPUT")
    lines = [f"{k}={v}" for k, v in values.items()]
    if path:
        with open(path, "a", encoding="utf8") as f:
            f.write("\n".join(lines) + "\n")
    print("\n".join(lines))


def normalize(s):
    return "".join(c.lower() for c in s if c.isalnum())


def git(*args, cwd):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def zip_manifest(path):
    with zipfile.ZipFile(path) as z:
        names = [n.replace("\\", "/") for n in z.namelist()]
        hits = [n for n in z.namelist() if n.replace("\\", "/") == "manifest.json"]
        if len(hits) != 1:
            fail(f"{path.name}: manifest.json fehlt im Wurzelverzeichnis des ZIPs")
        raw = z.read(hits[0]).decode("utf-8-sig", "replace")
    try:
        return json.loads(raw), names
    except ValueError:
        return json.loads(re.sub(r'\\(?!["\\/bfnrtu])', r"\\\\", raw)), names


def cmd_resolve(a):
    cat = Path(a.catalog)
    zip_name = a.zip_file.strip()
    if "/" in zip_name or not zip_name.lower().endswith(".zip"):
        fail(f"Ungültiger ZIP-Name '{zip_name}' (erwartet <Name>-<Version>.zip)")
    zpath = cat / zip_name
    if not zpath.is_file():
        fail(f"{zip_name} liegt nicht im Katalog-Zweig")
    m, entries = zip_manifest(zpath)
    pid, version = m.get("id", ""), m.get("version", "")
    if m.get("library") or m.get("libraries"):
        fail(f"{zip_name}: natives Plugin (\"library\") – dafür ist .github/workflows/build-plugin.yml zuständig")
    if not m.get("main"):
        fail(f"{zip_name}: manifest.json ohne \"main\" – kein QML-Plugin")

    prefix = re.match(r"^(.+?)[-_ ]v?\d+(?:\.\d+)*\.zip$", zip_name, re.I)
    keys = {normalize(k) for k in ((prefix.group(1) if prefix else ""), m.get("name", ""), pid.split(".")[-1], pid) if k}
    srcroot = cat / "sources"
    dirs = [d.name for d in srcroot.iterdir() if d.is_dir()] if srcroot.is_dir() else []
    hits = [d for d in dirs if normalize(d) in keys]
    if len(hits) != 1:
        fail(f"{zip_name}: {'kein' if not hits else 'mehr als ein'} passender Quellordner sources/<Name>/ (gesucht: {sorted(keys)}, vorhanden: {dirs})")
    src = srcroot / hits[0]
    cfg_path = src / "uve-build.json"
    if not cfg_path.is_file():
        fail(f"sources/{hits[0]}/uve-build.json fehlt (mit \"qmlModules\")")
    mods = json.loads(cfg_path.read_text(encoding="utf-8-sig")).get("qmlModules") or []
    if not mods:
        fail(f"sources/{hits[0]}/uve-build.json: \"qmlModules\" ist leer")
    cmake = (src / "CMakeLists.txt").read_text(encoding="utf8", errors="replace") if (src / "CMakeLists.txt").is_file() else ""
    defined = set(re.findall(r"add_library\(\s*([A-Za-z0-9_.+-]+)\s+MODULE\b", cmake))

    modules, missing_any = [], False
    for mod in mods:
        t, d = mod.get("target", ""), mod.get("dir", "").strip("/")
        if not re.match(r"^[A-Za-z0-9_]+$", t) or not re.match(r"^[A-Za-z0-9_][A-Za-z0-9_/-]*$", d) or ".." in d:
            fail(f"uve-build.json: ungültiges Modul {mod}")
        if t not in defined:
            fail(f"sources/{hits[0]}/CMakeLists.txt definiert kein add_library({t} MODULE …)")
        if f"{d}/qmldir" not in entries:
            fail(f"{zip_name}: {d}/qmldir fehlt im Paket")
        win = f"{d}/{FILE_OF['windows-x64'].format(t=t)}"
        if win not in entries:
            print(f"::warning::{zip_name}: {win} fehlt – unter Windows ist das Modul nicht vorhanden")
        files = {k: f"{d}/{FILE_OF[k].format(t=t)}" for k in TARGETS}
        if not a.force and all(f in entries for f in files.values()):
            print(f"{zip_name}: {d}/ enthält schon {', '.join(files.values())}")
        else:
            missing_any = True
        modules.append({"target": t, "dir": d, "check": mod.get("check", ""), "files": files})

    if not missing_any:
        print(f"{zip_name}: alle Module für {', '.join(TARGETS)} schon im Paket – nichts zu tun")
        out(skip="true", zip_name=zip_name)
        return

    # Qt-Version des Players (wie build-plugin.yml): player.ref aus plugin-builds.json → release.yml
    cfg = json.loads(CONFIG.read_text(encoding="utf8"))
    player = cfg.get("player", {})
    # Kein Tag nötig: Standard ist der Player-Zweig; fehlt der angegebene Ref (z. B. gelöschter Tag), ebenfalls
    repo, ref = player.get("repository", ""), player.get("ref") or "uve-player"
    release = None
    for r in dict.fromkeys([ref, "uve-player"]):
        url = f"https://raw.githubusercontent.com/{repo}/{r}/.github/workflows/release.yml"
        try:
            release = urllib.request.urlopen(url, timeout=30).read().decode("utf8")
            break
        except Exception as e:
            print(f"::warning::{url} nicht ladbar: {e}")
    if release is None:
        fail(f"release.yml des Players {repo} nicht ladbar")
    qt = re.search(r'^\s*QT_VERSION:\s*"?([0-9.]+)"?', release, re.M)
    if not qt:
        fail(f"Qt-Version in {url} nicht gefunden")

    sha = git("rev-parse", "HEAD", cwd=cat)
    base_blob = git("rev-parse", f"HEAD:{zip_name}", cwd=cat)
    matrix = {"include": [{"key": k, "os": RUNNERS[k]} for k in TARGETS]}
    print(f"{zip_name}: {pid} {version}, Quellen sources/{hits[0]}, Module {[x['target'] for x in modules]}, Qt {qt.group(1)}")
    out(skip="false", zip_name=zip_name, plugin_id=pid, version=version, catalog_sha=sha, base_blob=base_blob,
        source_path=f"sources/{hits[0]}", qt_version=qt.group(1),
        modules=json.dumps(modules, separators=(",", ":")), matrix=json.dumps(matrix, separators=(",", ":")))


def cmd_arch(a):
    data = Path(a.module).read_bytes()
    platform, arch = a.key.split("-", 1)
    found = None
    if data[:4] == b"\x7fELF":
        found = ("linux", {62: "x86_64", 183: "aarch64"}.get(struct.unpack_from("<H", data, 18)[0], "?"))
    elif data[:4] in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe"):
        found = ("macos", {0x0100000C: "arm64", 0x01000007: "x86_64"}.get(struct.unpack_from("<I", data, 4)[0], "?"))
    elif data[:2] == b"MZ":
        found = ("windows", "?")
    if not found:
        fail(f"{a.module}: unbekanntes Dateiformat")
    if found != (platform, arch):
        fail(f"{a.module}: Modul ist {found[0]}-{found[1]}, erwartet {a.key}")
    print(f"ok: {a.module} ist {a.key}")


def cmd_checkqml(a):
    """Kleine QML-Datei, die den Ordner wie das Plugin importiert und ein Objekt daraus erzeugt."""
    d = Path(a.dir).resolve().as_posix()
    url = ("file://" if d.startswith("/") else "file:///") + d
    obj = a.check.strip() or "QtObject {}"
    source = json.dumps('import "' + url + '"' + chr(10) + obj)
    lines = [
        "import QtQml",
        "QtObject {",
        "    Component.onCompleted: {",
        "        try {",
        "            var o = Qt.createQmlObject(" + source + ', this, "check")',
        '            console.log("QMLMODULE_OK " + o)',
        "            Qt.exit(0)",
        "        } catch (e) {",
        '            console.log("QMLMODULE_FAIL " + e)',
        "            Qt.exit(3)",
        "        }",
        "    }",
        "}",
    ]
    qml = chr(10).join(lines) + chr(10)
    Path(a.out).write_text(qml, encoding="utf8")
    print(qml)


def cmd_assemble(a):
    base = Path(a.base)
    parts = Path(a.parts)
    modules = json.loads(a.modules)
    outdir = Path(a.out)
    outdir.mkdir(parents=True, exist_ok=True)
    m, entries = zip_manifest(base)
    add = {}
    for mod in modules:
        for key, rel in mod["files"].items():
            p = parts / key / rel
            if not p.is_file():
                fail(f"Gebautes Modul {key}/{rel} fehlt – kein Paket")
            add[rel] = p
    target = outdir / base.name
    with zipfile.ZipFile(base) as src, zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as dst:
        for info in src.infolist():
            if info.filename.replace("\\", "/") in add:
                continue                              # wird durch das neu gebaute Modul ersetzt
            dst.writestr(info, src.read(info.filename))
        for rel, p in sorted(add.items()):
            zi = zipfile.ZipInfo(rel, date_time=(2026, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o755 << 16
            dst.writestr(zi, p.read_bytes())
    # Prüfen: Manifest unverändert, alle Module und qmldir vorhanden
    m2, entries2 = zip_manifest(target)
    if m2.get("id") != m.get("id") or m2.get("version") != m.get("version"):
        fail("Manifest im fertigen ZIP passt nicht zum hochgeladenen Paket")
    for mod in modules:
        need = [f"{mod['dir']}/qmldir"] + list(mod["files"].values())
        lack = [n for n in need if n not in entries2]
        if lack:
            fail(f"Im fertigen ZIP fehlen {lack}")
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    print(f"{target.name}: {len(entries2)} Dateien, SHA-256 {digest}")
    for n in sorted(entries2):
        print("  ", n)
    out(zip=str(target), file=target.name, sha256=digest)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("resolve")
    r.add_argument("--zip-file", required=True)
    r.add_argument("--catalog", required=True)
    r.add_argument("--force", action="store_true")
    c = sub.add_parser("arch")
    c.add_argument("--module", required=True)
    c.add_argument("--key", required=True)
    q = sub.add_parser("checkqml")
    q.add_argument("--dir", required=True)
    q.add_argument("--check", default="")
    q.add_argument("--out", required=True)
    s = sub.add_parser("assemble")
    s.add_argument("--base", required=True)
    s.add_argument("--parts", required=True)
    s.add_argument("--modules", required=True)
    s.add_argument("--out", required=True)
    a = ap.parse_args()
    {"resolve": cmd_resolve, "arch": cmd_arch, "checkqml": cmd_checkqml, "assemble": cmd_assemble}[a.cmd](a)


if __name__ == "__main__":
    main()
