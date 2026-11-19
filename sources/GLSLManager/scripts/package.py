"""Packt build/package/GLSLManager nach dist/GLSLManager-<version>.zip (manifest.json im Wurzelverzeichnis)."""
import json
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PKG = ROOT / "build" / "package" / "GLSLManager"
version = json.loads((PKG / "manifest.json").read_text(encoding="utf8"))["version"]
out = ROOT / "dist" / f"GLSLManager-{version}.zip"
out.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(PKG.rglob("*")):
        if f.is_file():
            z.write(f, f.relative_to(PKG).as_posix())
with zipfile.ZipFile(out) as z:
    for i in z.infolist():
        print(f"{i.file_size:>9} {i.filename}")
print(out, out.stat().st_size)
