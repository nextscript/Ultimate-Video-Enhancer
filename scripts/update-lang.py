#!/usr/bin/env python3
"""UVE Player – Sprachdateien (Qt Linguist) pflegen.

  python -I scripts/update-lang.py              alle lang/uve_*.ts mit den aktuellen Texten aus dem Quellcode abgleichen
                                                (lupdate: neue Texte werden als "unfinished" ergänzt, Übersetzungen bleiben)
  python -I scripts/update-lang.py --add fr     neue Sprache anlegen: lang/uve_fr.ts (leer, zum Übersetzen in Qt Linguist)
  python -I scripts/update-lang.py --check      prüfen: offene Einträge, Platzhalter %1…, Leerzeichen am Rand
  python -I scripts/update-lang.py --release    .qm-Dateien direkt nach lang/ erzeugen (sonst macht das der CMake-Build)

Benötigt die Qt Linguist Tools (lupdate/lrelease) aus QT_DIR (Standard M:/LAPS/Qt/6.12.0/msvc2022_64).
"""
import os, re, subprocess, sys, xml.etree.ElementTree as ET

sys.stdout.reconfigure(encoding="utf-8")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANG = os.path.join(ROOT, "lang")
QT_BIN = os.path.join(os.environ.get("QT_DIR", "M:/LAPS/Qt/6.12.0/msvc2022_64"), "bin")
PH = re.compile(r"%\d")


def ts_files():
    return sorted(os.path.join(LANG, f) for f in os.listdir(LANG) if re.fullmatch(r"uve_[a-z]{2,3}(_[A-Z]{2})?\.ts", f))


def lupdate(files):
    subprocess.run([os.path.join(QT_BIN, "lupdate.exe"), "-extensions", "cpp,h,qml", "-source-language", "de", "-no-obsolete",
                    "-locations", "none", "app", "src", "plugins", "-ts", *files], cwd=ROOT, check=True)


def check(path):
    problems, total, open_ = [], 0, 0
    for msg in ET.parse(path).getroot().iter("message"):
        src = msg.findtext("source") or ""
        tr = msg.find("translation")
        total += 1
        if tr is None or tr.get("type") == "unfinished" or not (tr.text or ""):
            open_ += 1
            continue
        v = tr.text
        if sorted(PH.findall(src)) != sorted(PH.findall(v)):
            problems.append(f"Platzhalter: {src!r} → {v!r}")
        if src.startswith(" ") != v.startswith(" ") or src.endswith(" ") != v.endswith(" "):
            problems.append(f"Leerzeichen am Rand: {src!r} → {v!r}")
    return total, open_, problems


def main():
    args = sys.argv[1:]
    if "--add" in args:
        code = args[args.index("--add") + 1]
        if not re.fullmatch(r"[a-z]{2,3}(_[A-Z]{2})?", code):
            sys.exit("Sprachkürzel wie fr, pt_BR erwartet")
        lupdate([os.path.join(LANG, f"uve_{code}.ts")])
        print(f"angelegt: lang/uve_{code}.ts – jetzt mit Qt Linguist übersetzen")
        return 0
    if "--check" not in args:
        lupdate(ts_files())
    rc = 0
    for p in ts_files():
        total, open_, problems = check(p)
        print(f"{os.path.basename(p)}: {total - open_}/{total} übersetzt")
        for pr in problems:
            print("   WARNUNG:", pr); rc = 1
    if "--release" in args:
        for p in ts_files():
            subprocess.run([os.path.join(QT_BIN, "lrelease.exe"), "-silent", p], check=True)
        print("qm-Dateien in lang/ erzeugt")
    return rc


if __name__ == "__main__":
    sys.exit(main())
