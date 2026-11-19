"""Testet das Plugin im echten Player mit einem isolierten Profil (UVE_DATA_DIR).

Aufruf: python -I run.py <ausgabeordner> [szenario ...]
Jedes Szenario legt filters.json an, startet UVEPlayer --selftest und sammelt Log und Bildschirmfotos.
"""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLAYER = Path(r"M:\LAPS\UVE Player\build\bin\UVEPlayer.exe")
VIDEO = Path(r"M:\LAPS\UVE Player\tests\media\test-2audio-sub.mkv")
PKG = ROOT / "build" / "package" / "GLSLManager"
ID = "org.uveplayer.glsl-manager"
QT_BIN = "M:/LAPS/Qt/6.12.0/msvc2022_64/bin"
EXAMPLES = json.loads((ROOT / "tests" / "gvf_filter_codes_example.json").read_text(encoding="utf8"))["codes"]


def e(label, code, **kw):
    return {"id": "t_" + label.replace(" ", "_"), "label": label, "type": "webgl", "enabled": True, "code": code, **kw}


def src(name):
    return (ROOT / name).read_text(encoding="utf8")


PASS = "void main(){ fragColor = texture(u_video, v_uv); }"
MODES = ["normal", "darken", "multiply", "color-burn", "lighten", "screen", "color-dodge", "overlay", "soft-light",
         "hard-light", "difference", "exclusion", "hue", "saturation", "color", "luminosity"]
SCENARIOS = {
    "baseline": [],
    "matrix": [e("Matrix", src("matrix.txt"))],
    "anime4k": [e("Anime4K", src("anime4k.txt"))],
    "both": [e("Anime4K", src("anime4k.txt")), e("Matrix", src("matrix.txt"))],
    "pass": [e("Durchreichen", PASS)],
    "flip": [e("Oben rot", "void main(){ fragColor = v_uv.y > 0.5 ? vec4(1.0,0.0,0.0,1.0) : vec4(0.0,0.0,1.0,1.0); }")],
    "examples": EXAMPLES,
    "forms": [
        e("Shadertoy", "void mainImage(out vec4 c, in vec2 p){ c = texture(iChannel0, p / iResolution.xy) * vec4(1.0, 0.8, 0.8, 1.0); }"),
        e("Hilfsfunktion", "vec3 fx(sampler2D t, vec2 uv, vec2 r){ return texture(t, uv + vec2(1.0, 0.0) / r).rgb; }"),
        e("Rumpf", "fragColor = texture(u_video, v_uv).bgra;"),
        e("Alt", "#version 300 es\nprecision highp float;\nuniform sampler2D u_video;\nuniform vec2 u_res;\nin vec2 vUv;\nout vec4 col;\nvoid main(){ col = texture2D(u_video, vUv); }"),
        e("Stats", "void main(){ vec4 c = texture(u_video, v_uv); fragColor = vec4(c.rgb * (u_avg_lum + u_contrast + u_avg_r + u_avg_g + u_avg_b), 1.0); }"),
        e("TexFetch", "void main(){ fragColor = texelFetch(u_video, ivec2(v_uv * u_res), 0) * textureLod(u_video, v_uv, 0.0); }"),
        e("Coord", "void main(){ fragColor = texture(u_video, gl_FragCoord.xy / u_res); }"),
        e("Param", "//!PARAM stark\n//!TYPE float\n//!MINIMUM 0\n//!MAXIMUM 2\n0.9\n\n//!HOOK MAIN\n//!BIND HOOKED\nvec4 hook(){ return HOOKED_tex(HOOKED_pos) * stark; }\n"),
    ],
    "blend": [e("B " + m, "void main(){ fragColor = vec4(texture(u_video, v_uv).gbr, 0.7); }", blendMode=m) for m in MODES],
    "broken": [
        e("Kaputt", "void main(){ fragColor = kaputt(1.0); }"),
        e("Gut", "void main(){ fragColor = texture(u_video, v_uv) * vec4(1.0, 0.6, 0.6, 1.0); }"),
        e("Auch kaputt", "void main(){ int x = vec3(1.0); fragColor = vec4(1.0); }"),
        e("Noch gut", "void main(){ fragColor = texture(u_video, v_uv); }"),
    ],
}


def run(name, out):
    data = out / name
    if data.exists():
        shutil.rmtree(data)
    plugins = data / "plugins"
    shutil.copytree(PKG, plugins / ID)
    shutil.copytree(ROOT / "tests" / "integration" / "driver", plugins / "glsl-test-driver")
    pdata = data / "plugin-data" / ID
    pdata.mkdir(parents=True)
    (pdata / "filters.json").write_text(json.dumps({"schema": "gvf-custom-filter-codes", "ver": 1, "codes": SCENARIOS[name]}), encoding="utf8")
    (data / "settings.json").write_text(json.dumps({"language": "de", "checkToolUpdates": False, "checkPlayerUpdates": False}), encoding="utf8")
    res = data / "result"
    res.mkdir()
    env = dict(os.environ, UVE_DATA_DIR=str(data), UVE_SELFTEST_PLAYSEC="13", UVE_SELFTEST_UI="0",
               PATH=QT_BIN + ";" + os.environ["PATH"])
    try:
        code = subprocess.run([str(PLAYER), "--selftest", str(res / "out.json"), str(VIDEO)], env=env, timeout=120).returncode
    except subprocess.TimeoutExpired:
        code = "timeout"
    print(f"=== {name}: exit {code}")
    for log in sorted((data / "logs").glob("*")) if (data / "logs").exists() else []:
        lines = log.read_text(encoding="utf8", errors="replace").splitlines()
        hits = [l for l in lines if any(k in l for k in ("GLSLTEST", "Shaderfehler", "shader", "glsl-manager", "qml", "rror"))]
        print(f"--- {log.name} ({len(lines)} Zeilen, {len(hits)} Treffer)")
        for l in hits[:40]:
            print("   ", l[:300])


def run_install(out):
    """Echte Installation aus dem ZIP: Start 1 merkt das Paket vor, Start 2 lädt es."""
    data = out / "install"
    if data.exists():
        shutil.rmtree(data)
    shutil.copytree(ROOT / "tests" / "integration" / "driver", data / "plugins" / "glsl-test-driver")
    (data / "settings.json").write_text(json.dumps({"language": "en"}), encoding="utf8")
    res = data / "result"
    res.mkdir()
    shutil.copy(ROOT / "dist" / "GLSLManager-1.0.0.zip", res / "GLSLManager.zip")
    env = dict(os.environ, UVE_DATA_DIR=str(data), UVE_SELFTEST_PLAYSEC="13", UVE_SELFTEST_UI="0",
               PATH=QT_BIN + ";" + os.environ["PATH"])
    for start in (1, 2):
        subprocess.run([str(PLAYER), "--selftest", str(res / "out.json"), str(VIDEO)], env=env, timeout=120)
        states = {p["id"]: p["state"] + (" " + p["error"] if p["error"] else "") for p in json.loads((res / "out.json").read_text(encoding="utf8"))["plugins"]}
        print(f"=== install, Start {start}: {states}")
        for l in (data / "logs" / "uve-player.log").read_text(encoding="utf8", errors="replace").splitlines():
            if "GLSLTEST" in l or "Paket" in l or "installiert" in l:
                print("   ", l[:260])
        (data / "logs" / "uve-player.log").unlink()
    print("    Dateien:", sorted(str(p.relative_to(data / "plugins" / ID)) for p in (data / "plugins" / ID).rglob("*") if p.is_file()))


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    if sys.argv[2:] == ["install"]:
        run_install(out)
        return
    for name in sys.argv[2:] or SCENARIOS:
        run(name, out)


main()
