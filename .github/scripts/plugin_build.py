"""Hilfsschritte für .github/workflows/build-plugin.yml (UVE-Player-Plugins für Windows, Linux und macOS).

  resolve   --id ID --version V [--source-ref REF] [--player-ref REF]
            Plugin aus .github/plugin-builds.json, Quellstand auflösen (Tag der Version, sonst sourceFallbackRef)
            und prüfen → $GITHUB_OUTPUT
  set-version --manifest FILE --version V      Version des Pakets ins Manifest des Quellstands schreiben
  arch      --module FILE --key TARGET          Architektur des Moduls prüfen (PE / ELF / Mach-O)
  assemble  --parts DIR --id ID --version V --package NAME --targets K1,K2,… --out DIR
            Module der Plattform-Jobs mit gemeinsamem Manifest und Ressourcen zu <NAME>-<V>.zip packen und prüfen

Ohne Erfolg endet jeder Schritt mit "error: …" und Exit-Code 1.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

sys.stdout.reconfigure(encoding="utf-8")
sys.stderr.reconfigure(encoding="utf-8")
ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / ".github" / "plugin-builds.json"
SHA_RE = re.compile(r"^[0-9a-f]{40}$")
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+([-+].*)?$")
ID_RE = re.compile(r"^[a-z0-9]+([.-][a-z0-9]+)+$")
# Dateiendung je Plattform des Zielschlüssels
SUFFIX = {"windows": ".dll", "linux": ".so", "macos": ".dylib"}


def fail(msg):
    print(f"::error::{msg}")
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def out(**values):
    path = os.environ.get("GITHUB_OUTPUT")
    lines = [f"{k}={v}" for k, v in values.items()]
    print("\n".join(lines))
    if path:
        with open(path, "a", encoding="utf8") as f:
            f.write("\n".join(lines) + "\n")


def git(*args, cwd=None, check=True):
    r = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True)
    if check and r.returncode:
        fail(f"git {' '.join(a if 'x-access-token' not in a else '<url>' for a in args)}: {r.stderr.strip()}")
    return r.stdout


def repo_url(repository, token_env):
    token = os.environ.get(token_env, "")
    return f"https://x-access-token:{token}@github.com/{repository}.git" if token else f"https://github.com/{repository}.git"


def lookup_ref(url, ref):
    """Tag, Zweig oder Commit → (Commit-SHA, Art) oder (None, None)."""
    if SHA_RE.match(ref):
        return ref, "commit"
    lines = git("ls-remote", url, f"refs/tags/{ref}", f"refs/tags/{ref}^{{}}", f"refs/heads/{ref}").splitlines()
    refs = {l.split("\t")[1]: l.split("\t")[0] for l in lines if "\t" in l}
    sha = refs.get(f"refs/tags/{ref}^{{}}") or refs.get(f"refs/tags/{ref}")
    if sha:
        return sha, "tag"
    if f"refs/heads/{ref}" in refs:
        return refs[f"refs/heads/{ref}"], "branch"   # einmal festgelegt: alle Plattform-Jobs bauen diesen Commit
    return None, None


def resolve_ref(url, ref, what):
    sha, kind = lookup_ref(url, ref)
    if not sha:
        fail(f"{what}: '{ref}' ist weder Tag, Zweig noch Commit")
    return sha, kind


def fetch_commit(url, sha, where):
    where.mkdir(parents=True, exist_ok=True)
    git("init", "-q", cwd=where)
    git("fetch", "-q", "--depth", "1", "--filter=blob:none", url, sha, cwd=where)


def show(where, sha, path):
    r = subprocess.run(["git", "show", f"{sha}:{path}"], cwd=where, capture_output=True)
    return r.stdout if r.returncode == 0 else None


def expected_filename(path):
    return PurePosixPath(path).name


def check_libraries(manifest, targets, where):
    libs = manifest.get("libraries")
    if not isinstance(libs, dict) or not libs:
        fail(f"{where}: manifest.json hat kein Feld \"libraries\" (Zielschlüssel → Bibliothek), erwartet: {', '.join(targets)}")
    missing = [k for k in targets if k not in libs]
    if missing:
        fail(f"{where}: \"libraries\" ohne Eintrag für {', '.join(missing)}")
    extra = [k for k in libs if k not in targets]
    if extra:
        fail(f"{where}: \"libraries\" nennt Ziele ohne Build in diesem Workflow: {', '.join(extra)}")
    for key, path in libs.items():
        p = PurePosixPath(path)
        if p.is_absolute() or ".." in p.parts or ":" in path or "\\" in path or not path:
            fail(f"{where}: ungültiger Pfad für {key}: '{path}' (muss im Plugin-Ordner liegen)")
        platform = key.split("-", 1)[0]
        if platform not in SUFFIX or not path.endswith(SUFFIX[platform]):
            fail(f"{where}: {key} → '{path}': erwartet eine {SUFFIX.get(platform, '?')}-Datei")
    plat = manifest.get("supportedPlatforms") or []
    for key in targets:
        if plat and key.split("-", 1)[0] not in plat:
            fail(f"{where}: supportedPlatforms {plat} enthält '{key.split('-', 1)[0]}' nicht")
    return libs


def cmd_resolve(a):
    cfg = json.loads(CONFIG.read_text(encoding="utf8"))
    plugins = cfg.get("plugins", {})
    if a.id not in plugins:
        fail(f"Plugin '{a.id}' ist nicht in .github/plugin-builds.json eingetragen (bekannt: {', '.join(plugins) or '–'})")
    if not VERSION_RE.match(a.version):
        fail(f"Ungültige Version '{a.version}' (erwartet z. B. 1.0.2)")
    p = plugins[a.id]
    for field in ("package", "sourceRef", "sourcePath", "target", "packageDir"):
        if not p.get(field):
            fail(f".github/plugin-builds.json: '{a.id}' ohne '{field}'")
    targets = cfg.get("targets") or ["windows-x64", "linux-x86_64", "macos-arm64"]

    src_repo = p.get("sourceRepository") or os.environ.get("GITHUB_REPOSITORY", "")
    src_url = repo_url(src_repo, "SOURCE_TOKEN")
    if a.source_ref:
        src_ref = a.source_ref
        src_sha, src_kind = resolve_ref(src_url, src_ref, f"Quellstand {src_repo}")
    else:
        # Tag der Version, falls es einen gibt; sonst der aktuelle Stand des Quellzweigs (z. B. uve-player-plugins)
        src_ref = p["sourceRef"].replace("{version}", a.version)
        src_sha, src_kind = lookup_ref(src_url, src_ref)
        if not src_sha:
            fallback = p.get("sourceFallbackRef")
            if not fallback:
                fail(f"Quellstand {src_repo}: Tag '{src_ref}' nicht gefunden und kein sourceFallbackRef eingetragen")
            print(f"Kein Tag {src_ref} – baue den aktuellen Stand von {fallback}")
            src_ref = fallback
            src_sha, src_kind = resolve_ref(src_url, src_ref, f"Quellstand {src_repo}")

    player = cfg.get("player", {})
    player_repo = player.get("repository") or os.environ.get("GITHUB_REPOSITORY", "")
    player_ref = a.player_ref or player.get("ref")
    if not player_ref:
        fail(".github/plugin-builds.json: player.ref fehlt (fester Player-Tag oder Commit)")
    player_url = repo_url(player_repo, "PLAYER_TOKEN")
    player_sha, _ = resolve_ref(player_url, player_ref, f"Player {player_repo}")

    tmp = Path(tempfile.mkdtemp())
    fetch_commit(src_url, src_sha, tmp / "src")
    base = p["sourcePath"].strip("/")
    raw = show(tmp / "src", src_sha, f"{base}/manifest.json")
    if raw is None:
        fail(f"Quellstand {src_ref} ({src_sha[:10]}): {base}/manifest.json fehlt – Plugin-Quellen nicht gefunden")
    cmake = show(tmp / "src", src_sha, f"{base}/CMakeLists.txt")
    if cmake is None:
        fail(f"Quellstand {src_ref} ({src_sha[:10]}): {base}/CMakeLists.txt fehlt – kein Build-Ziel")
    if not re.search(rf"add_library\(\s*{re.escape(p['target'])}\s+MODULE\b", cmake.decode("utf8", "replace")):
        fail(f"Quellstand {src_ref}: {base}/CMakeLists.txt definiert kein Build-Ziel 'add_library({p['target']} MODULE …)'")
    manifest = json.loads(raw.decode("utf8"))
    if manifest.get("id") != a.id:
        fail(f"Quellstand {src_ref}: manifest.json hat id '{manifest.get('id')}', erwartet '{a.id}'")
    if manifest.get("version") != a.version:
        print(f"::notice::manifest.json im Quellstand hat Version {manifest.get('version')} – das Paket bekommt {a.version}")
    check_libraries(manifest, targets, f"Quellstand {src_ref}")

    fetch_commit(player_url, player_sha, tmp / "player")
    if show(tmp / "player", player_sha, "src/plugins/PluginApi.h") is None:
        fail(f"Player {player_ref} ({player_sha[:10]}): src/plugins/PluginApi.h fehlt")
    release = (show(tmp / "player", player_sha, ".github/workflows/release.yml") or b"").decode("utf8")
    qt = re.search(r'^\s*QT_VERSION:\s*"?([0-9.]+)"?', release, re.M)
    if not qt:
        fail(f"Player {player_ref}: Qt-Version in .github/workflows/release.yml nicht gefunden")
    cml = (show(tmp / "player", player_sha, "CMakeLists.txt") or b"").decode("utf8")
    pv = re.search(r"project\(UVEPlayer VERSION ([0-9.]+)", cml)
    player_version = pv.group(1) if pv else ""
    min_host = manifest.get("minHostVersion", "")
    if player_version and min_host and tuple(map(int, player_version.split("."))) < tuple(map(int, min_host.split(".")[:3])):
        fail(f"minHostVersion {min_host} ist neuer als der Player {player_ref} ({player_version})")
    if player_version and (not min_host or tuple(map(int, min_host.split(".")[:3])) < (1, 2, 9)):
        fail(f"minHostVersion '{min_host}': Pakete mit \"libraries\" brauchen mindestens 1.2.9")
    shutil.rmtree(tmp, ignore_errors=True)

    out(package=p["package"], source_repo=src_repo, source_ref=src_ref, source_sha=src_sha, source_path=base,
        target=p["target"], package_dir=p["packageDir"], player_repo=player_repo, player_ref=player_ref,
        player_sha=player_sha, player_version=player_version,
        player_release=player_ref if player_ref.startswith("uve-player-v") else "",
        qt_version=qt.group(1), targets=",".join(targets), libraries=json.dumps(manifest["libraries"], separators=(",", ":")))


def cmd_set_version(a):
    p = Path(a.manifest)
    text = p.read_text(encoding="utf8")
    if not VERSION_RE.match(a.version):
        fail(f"Ungültige Version '{a.version}'")
    new, n = re.subn(r'("version"\s*:\s*)"[^"]*"', lambda m: f'{m.group(1)}"{a.version}"', text, count=1)
    if n != 1:
        fail(f"{p}: Feld version nicht gefunden")
    p.write_text(new, encoding="utf8", newline="\n")
    if json.loads(new).get("version") != a.version:
        fail(f"{p}: Version konnte nicht gesetzt werden")
    print(f"{p}: version = {a.version}")


def cmd_arch(a):
    data = Path(a.module).read_bytes()
    platform, arch = a.key.split("-", 1)
    found = None
    if data[:2] == b"MZ":
        off = struct.unpack_from("<I", data, 0x3C)[0]
        if data[off:off + 4] != b"PE\0\0":
            fail(f"{a.module}: kein PE-Modul")
        found = ("windows", {0x8664: "x64", 0xAA64: "arm64", 0x14C: "x86"}.get(struct.unpack_from("<H", data, off + 4)[0], "?"))
    elif data[:4] == b"\x7fELF":
        found = ("linux", {62: "x86_64", 183: "aarch64"}.get(struct.unpack_from("<H", data, 18)[0], "?"))
    elif data[:4] in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe"):
        found = ("macos", {0x0100000C: "arm64", 0x01000007: "x86_64"}.get(struct.unpack_from("<I", data, 4)[0], "?"))
    elif data[:4] == b"\xca\xfe\xba\xbe":
        fail(f"{a.module}: Universal-Binary – erwartet ein reines {arch}-Modul")
    if not found:
        fail(f"{a.module}: unbekanntes Dateiformat")
    if found != (platform, arch):
        fail(f"{a.module}: Modul ist {found[0]}-{found[1]}, erwartet {a.key}")
    print(f"ok: {a.module} ist {a.key}")


def files_of(root):
    return {p.relative_to(root).as_posix(): p for p in sorted(root.rglob("*")) if p.is_file()}


def normalize(s):
    return "".join(c.lower() for c in s if c.isalnum())


def cmd_assemble(a):
    targets = a.targets.split(",")
    parts = Path(a.parts)
    trees = {}
    for key in targets:
        d = parts / key
        if not d.is_dir() or not any(d.rglob("*")):
            fail(f"Build für {key} fehlt (kein Modul aus dem Plattform-Job) – kein Plugin-ZIP")
        trees[key] = files_of(d)

    # Manifest und gemeinsame Ressourcen: in allen Plattform-Jobs identisch (gleicher Quellstand)
    def common(tree):
        return {k: v for k, v in tree.items() if not k.startswith("bin/")}
    first = targets[0]
    shared = common(trees[first])
    if "manifest.json" not in shared:
        fail(f"Build {first}: manifest.json fehlt im Paketordner")
    for key in targets[1:]:
        other = common(trees[key])
        if set(other) != set(shared):
            fail(f"Gemeinsame Dateien von {key} und {first} unterscheiden sich: {sorted(set(other) ^ set(shared))}")
        for rel in shared:
            if rel.endswith(".qm"):
                continue   # lrelease schreibt je nach Plattform minimal andere Bytes; Inhalt kommt aus derselben .ts
            if shared[rel].read_bytes() != other[rel].read_bytes():
                fail(f"{rel} unterscheidet sich zwischen {first} und {key} – unterschiedlicher Quellstand?")

    manifest = json.loads(shared["manifest.json"].read_text(encoding="utf8"))
    if manifest.get("id") != a.id:
        fail(f"manifest.json: id '{manifest.get('id')}' != Eingabe '{a.id}'")
    if manifest.get("version") != a.version:
        fail(f"manifest.json: version '{manifest.get('version')}' != Eingabe '{a.version}'")
    if not ID_RE.match(a.id) or not VERSION_RE.match(a.version):
        fail("Ungültige id oder Version")
    libs = check_libraries(manifest, targets, "manifest.json")
    keys = {normalize(manifest.get("name", "")), normalize(a.id.split(".")[-1]), normalize(a.id)}
    if normalize(a.package) not in keys:
        fail(f"Paketname '{a.package}' passt nicht zu name/id des Manifests – der Plugin-Updater fände das Paket nicht")

    files = dict(shared)
    for key in targets:
        lib = libs[key]
        own = {k: v for k, v in trees[key].items() if k.startswith("bin/")}
        if lib not in own:
            fail(f"Build {key}: {lib} fehlt (gebaut: {', '.join(own) or 'nichts'})")
        folder = PurePosixPath(lib).parent.as_posix() + "/"
        stray = [k for k in own if not k.startswith(folder)]
        if stray:
            fail(f"Build {key}: Dateien außerhalb von {folder}: {stray}")
        files.update(own)   # Modul und etwaige private Begleitbibliotheken im selben Ordner

    zip_path = Path(a.out) / f"{a.package}-{a.version}.zip"
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel in ["manifest.json"] + sorted(k for k in files if k != "manifest.json"):
            info = zipfile.ZipInfo(rel, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, files[rel].read_bytes())

    # Prüfung des fertigen ZIPs: Manifest oben, alle referenzierten Dateien und Module enthalten
    with zipfile.ZipFile(zip_path) as z:
        names = set(z.namelist())
        bad = z.testzip()
        if bad:
            fail(f"{zip_path.name}: beschädigter Eintrag {bad}")
        packed = json.loads(z.read("manifest.json"))
        if packed != manifest:
            fail(f"{zip_path.name}: manifest.json weicht ab")
        for key, lib in libs.items():
            if lib not in names:
                fail(f"{zip_path.name}: {lib} ({key}) fehlt")
        refs = [p.get("qml") for p in packed.get("contributes", {}).get("panels", []) if p.get("qml") and not p["qml"].startswith("qrc:")]
        if packed.get("main"):
            refs.append(packed["main"])
        for ref in refs:
            if ref not in names:
                fail(f"{zip_path.name}: im Manifest genannte Datei {ref} fehlt")
        tr = packed.get("contributes", {}).get("translations")
        if tr and not any(n.startswith(tr.rstrip("/") + "/") for n in names):
            fail(f"{zip_path.name}: Sprachordner {tr} fehlt")
        for info in z.infolist():
            print(f"{info.file_size:>10}  {info.filename}")
    digest = hashlib.sha256(zip_path.read_bytes()).hexdigest()
    print(f"\n{zip_path.name}  {zip_path.stat().st_size} Bytes  SHA-256 {digest}")
    out(zip=str(zip_path), name=zip_path.stem, sha256=digest)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("resolve")
    r.add_argument("--id", required=True)
    r.add_argument("--version", required=True)
    r.add_argument("--source-ref", default="")
    r.add_argument("--player-ref", default="")
    v = sub.add_parser("set-version")
    v.add_argument("--manifest", required=True)
    v.add_argument("--version", required=True)
    c = sub.add_parser("arch")
    c.add_argument("--module", required=True)
    c.add_argument("--key", required=True)
    s = sub.add_parser("assemble")
    for arg in ("--parts", "--id", "--version", "--package", "--targets", "--out"):
        s.add_argument(arg, required=True)
    a = ap.parse_args()
    {"resolve": cmd_resolve, "set-version": cmd_set_version, "arch": cmd_arch, "assemble": cmd_assemble}[a.cmd](a)


if __name__ == "__main__":
    main()
