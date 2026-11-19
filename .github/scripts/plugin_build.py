"""Hilfsschritte für .github/workflows/build-plugin.yml (UVE-Player-Plugins für Windows, Linux und macOS).

  resolve   [--zip-file NAME] [--id ID --version V] [--source-ref REF] [--player-ref REF]
            Paket im Katalog-Zweig, Quellen (sources/<Name>/ oder .github/plugin-builds.json), Build-Ziel, Player
            und Modus bestimmen → $GITHUB_OUTPUT
  set-version --manifest FILE --version V      Version des Pakets ins Manifest des Quellstands schreiben
  locate    --target T --key K --roots A,B      gebautes Modul eines CMake-Ziels finden
  arch      --module FILE --key TARGET          Architektur des Moduls prüfen (PE / ELF / Mach-O)
  assemble  --parts DIR --id ID --version V --package NAME --targets K1,K2,… --out DIR [--base-zip ZIP --built …]
            Module zu einem ZIP packen (mit --base-zip: in das hochgeladene Windows-ZIP einfügen) und prüfen

Modi:
  insert  Im Katalog-Zweig liegt <Name>-<Version>.zip als reines Windows-Paket: nur Linux und macOS werden aus den
          Quellen gebaut und neben die Windows-DLL in dieses ZIP gelegt. "library" und minHostVersion bleiben
          (ältere Windows-Player laden weiter die DLL), "libraries" kommt dazu.
  full    Plugin mit packageDir in plugin-builds.json und ohne hochgeladenes ZIP: alle drei Plattformen bauen.

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


def load_json_lenient(raw, where):
    """JSON wie Qt es liest: ungültige Escapes (z. B. Backslash-Punkt in regulären Ausdrücken) als Text behandeln."""
    text = raw.decode("utf-8-sig", "replace") if isinstance(raw, bytes) else raw
    try:
        return json.loads(text)
    except ValueError:
        try:
            return json.loads(re.sub(r'\\(?!["\\/bfnrtu])', r"\\\\", text))
        except ValueError as e:
            fail(f"{where}: manifest.json ist kein gültiges JSON ({e})")


def zip_key(name):
    m = re.match(r"^(.+?)[-_ ]v?(\d+(?:\.\d+)*)\.zip$", name, re.I)   # wie PluginUpdater::splitPackageName
    return (m.group(1), m.group(2)) if m else (None, None)


def cmd_resolve(a):
    cfg = json.loads(CONFIG.read_text(encoding="utf8"))
    plugins = cfg.get("plugins", {})
    targets = cfg.get("targets") or ["windows-x64", "linux-x86_64", "macos-arm64"]
    repo = os.environ.get("GITHUB_REPOSITORY", "")
    tmp = Path(tempfile.mkdtemp())

    # ---- Katalog-Zweig: hochgeladene Pakete und Quellen (sources/<Name>/) im selben Commit
    catalog = cfg.get("catalogBranch", "uve-player-plugins")
    cat_url = os.environ.get("PLUGIN_CATALOG_URL") or repo_url(repo, "CATALOG_TOKEN")   # Override nur für lokale Tests
    cat_sha, _ = resolve_ref(cat_url, catalog, f"Katalog-Zweig {catalog}")
    cat = tmp / "catalog"
    cat.mkdir()
    git("init", "-q", cwd=cat)
    git("fetch", "-q", "--depth", "1", cat_url, cat_sha, cwd=cat)
    root_files = git("ls-tree", "--name-only", cat_sha, cwd=cat).split()

    zip_name = a.zip_file.strip()
    if not zip_name:
        if not a.id or not a.version:
            fail("Bitte zip_file angeben (z. B. kickchat-1.3.2.zip) oder Plugin-ID und Version")
        entry = plugins.get(a.id)
        guess = f"{entry['package']}-{a.version}.zip" if entry and entry.get("package") else ""
        if guess in root_files:
            zip_name = guess
        else:   # Paket dieser ID und Version im Katalog suchen
            for f in root_files:
                if f.lower().endswith(".zip"):
                    m = read_zip_manifest(show(cat, cat_sha, f), f)
                    if m.get("id") == a.id and m.get("version") == a.version:
                        zip_name = f
                        break
        zip_name = zip_name or guess
    if "/" in zip_name or not zip_name.lower().endswith(".zip"):
        fail(f"Ungültiger ZIP-Name '{zip_name}' (erwartet <Name>-<Version>.zip im Wurzelverzeichnis von {catalog})")
    prefix, _ = zip_key(zip_name)

    base_blob, base = "", None
    if zip_name in root_files:
        base_blob = git("rev-parse", f"{cat_sha}:{zip_name}", cwd=cat).strip()
        base = read_zip_manifest(show(cat, cat_sha, zip_name), zip_name)
    pid = (base or {}).get("id") or a.id
    version = (base or {}).get("version") or a.version
    if a.id and pid != a.id:
        fail(f"{zip_name} enthält das Plugin '{pid}', erwartet '{a.id}'")
    if a.version and version != a.version:
        fail(f"{zip_name} hat Version '{version}', erwartet '{a.version}'")
    if not pid or not VERSION_RE.match(version or ""):
        fail(f"{zip_name} liegt nicht in {catalog} – ohne hochgeladenes Paket bitte Plugin-ID und Version angeben")
    entry = plugins.get(pid, {})

    if base and base.get("libraries"):
        fail(f"{zip_name} ist schon ein Mehrplattform-Paket (manifest.json mit \"libraries\") – nichts zu tun")
    if base:
        mode = "insert"
        if not base.get("library"):
            fail(f"{zip_name}: manifest.json ohne \"library\" – reine QML-Plugins laufen schon überall, native Pakete brauchen das Windows-Modul")
    elif entry.get("packageDir"):
        mode = "full"
    else:
        fail(f"{zip_name} liegt nicht in {catalog} – zuerst das Windows-ZIP hochladen")
    package = entry.get("package") or prefix or pid.split(".")[-1]

    # ---- Quellen: Eintrag in plugin-builds.json, sonst sources/<Name>/ im Katalog-Zweig
    if entry.get("sourcePath"):
        src_repo = entry.get("sourceRepository") or repo
        src_url = repo_url(src_repo, "SOURCE_TOKEN")
        if a.source_ref:
            src_ref = a.source_ref
            src_sha, _ = resolve_ref(src_url, src_ref, f"Quellstand {src_repo}")
        else:
            src_ref = entry.get("sourceRef", "").replace("{version}", version)
            src_sha, _ = lookup_ref(src_url, src_ref) if src_ref else (None, None)
            if not src_sha:
                src_ref = entry.get("sourceFallbackRef") or catalog
                src_sha, _ = resolve_ref(src_url, src_ref, f"Quellstand {src_repo}")
        src_path = entry["sourcePath"].strip("/")
        if src_sha == cat_sha:
            src = cat
        else:
            src = tmp / "src"
            fetch_commit(src_url, src_sha, src)
    else:
        src_repo, src_ref, src_sha, src, src_url = repo, catalog, cat_sha, cat, cat_url
        dirs = [d.split("/", 1)[1] for d in git("ls-tree", "-d", "--name-only", cat_sha, "sources/", cwd=cat).split("\n") if d]
        keys = {normalize(k) for k in (prefix or "", (base or {}).get("name", ""), pid.split(".")[-1], pid) if k}
        hits = [d for d in dirs if normalize(d) in keys]
        if not hits:
            fail(f"Keine Quellen für {pid}: im Zweig {catalog} fehlt der Ordner sources/{prefix or package}/ "
                 f"(CMakeLists.txt mit add_library(… MODULE …) und Quellcode). Vorhanden: {', '.join(dirs) or '–'}")
        if len(hits) > 1:
            fail(f"Mehrere Quellordner passen zu {pid}: {hits}")
        src_path = f"sources/{hits[0]}"

    cmake = show(src, src_sha, f"{src_path}/CMakeLists.txt")
    if cmake is None:
        fail(f"{src_path}/CMakeLists.txt fehlt ({src_ref}, {src_sha[:10]}) – kein Build-Ziel")
    modules = re.findall(r"add_library\(\s*([A-Za-z0-9_.+-]+)\s+MODULE\b", cmake.decode("utf8", "replace"))
    target = entry.get("target")
    if not target:
        stem = PurePosixPath(base["library"]).stem if base else ""
        same = [m for m in modules if m == stem or normalize(m) == normalize(stem)]
        target = (same or (modules if len(modules) == 1 else [None]))[0]
        if not target:
            fail(f"{src_path}/CMakeLists.txt: Build-Ziel nicht eindeutig (Module: {modules or '–'}, DLL im ZIP: {stem or '–'})")
    if target not in modules:
        fail(f"{src_path}/CMakeLists.txt definiert kein Build-Ziel 'add_library({target} MODULE …)'")
    src_manifest_raw = show(src, src_sha, f"{src_path}/manifest.json")
    if src_manifest_raw is not None:
        sm = load_json_lenient(src_manifest_raw, f"{src_path}/manifest.json")
        if sm.get("id") != pid:
            fail(f"{src_path}/manifest.json gehört zu '{sm.get('id')}', das Paket zu '{pid}'")
        if sm.get("version") != version:
            print(f"::warning::{src_path}/manifest.json hat Version {sm.get('version')}, das Paket {version} – "
                  f"sind die Quellen aktuell? Gebaut wird mit Version {version}.")

    # ---- Player (Plugin-API, Qt-Version)
    player = cfg.get("player", {})
    player_repo = player.get("repository") or repo
    player_ref = a.player_ref or player.get("ref")
    if not player_ref:
        fail(".github/plugin-builds.json: player.ref fehlt (fester Player-Tag oder Commit)")
    player_url = repo_url(player_repo, "PLAYER_TOKEN")
    player_sha, _ = resolve_ref(player_url, player_ref, f"Player {player_repo}")
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

    # ---- Wohin die Module kommen
    win = [k for k in targets if k.startswith("windows-")]
    if mode == "insert":
        lib = base["library"].replace("\\", "/")
        folder = PurePosixPath(lib).parent.as_posix()
        folder = "" if folder == "." else folder + "/"
        stem = PurePosixPath(lib).stem
        libraries = {}
        for k in targets:
            plat = k.split("-", 1)[0]
            libraries[k] = lib if plat == "windows" else f"{folder}lib{stem}{SUFFIX[plat]}"
        existing = set(zip_entries(show(cat, cat_sha, zip_name), zip_name))
        clash = [v for k, v in libraries.items() if not k.startswith("windows-") and v in existing]
        if clash:
            fail(f"{zip_name} enthält schon {clash} – Mehrplattform-Paket? Dann ist nichts zu tun")
        build = [k for k in targets if k not in win]
    else:
        sm = load_json_lenient(src_manifest_raw or b"{}", f"{src_path}/manifest.json")
        libraries = check_libraries(sm, targets, f"{src_path}/manifest.json")
        if player_version and version_tuple(sm.get("minHostVersion", "0")) < (1, 2, 9):
            fail(f"{src_path}/manifest.json: minHostVersion muss für \"libraries\" mindestens 1.2.9 sein")
        build = list(targets)
    runners = {"windows-x64": "windows-2022", "linux-x86_64": "ubuntu-24.04", "macos-arm64": "macos-latest"}
    matrix = {"include": [{"key": k, "os": runners[k]} for k in build]}
    print(f"{zip_name}: {pid} {version}, Modus {mode}, Quellen {src_path} ({src_ref} → {src_sha[:10]}), Ziel {target}, "
          f"gebaut: {', '.join(build)}")
    shutil.rmtree(tmp, ignore_errors=True)

    out(mode=mode, plugin_id=pid, version=version, catalog_branch=catalog, catalog_sha=cat_sha, zip_name=zip_name,
        base_blob=base_blob, build_targets=",".join(build), matrix=json.dumps(matrix, separators=(",", ":")),
        package=package, source_repo=src_repo, source_ref=src_ref, source_sha=src_sha, source_path=src_path,
        target=target, package_dir=entry.get("packageDir", ""), run_tests="true" if entry.get("tests", mode == "full") else "false",
        player_repo=player_repo, player_ref=player_ref, player_sha=player_sha, player_version=player_version,
        player_release=player_ref if player_ref.startswith("uve-player-v") else "",
        qt_version=qt.group(1), targets=",".join(targets), libraries=json.dumps(libraries, separators=(",", ":")))


def cmd_locate(a):
    """Gebautes Modul eines CMake-Ziels finden (Ausgabeordner legt jedes Plugin selbst fest)."""
    plat = a.key.split("-", 1)[0]
    t = a.target
    names = {"windows": [f"{t}.dll"], "linux": [f"lib{t}.so", f"{t}.so"],
             "macos": [f"lib{t}.dylib", f"lib{t}.so", f"{t}.dylib", f"{t}.so", f"lib{t}.bundle", f"{t}.bundle"]}[plat]
    found = []
    for r in a.roots.split(","):
        rp = Path(r)
        if rp.is_dir():
            found += [f for f in rp.rglob("*") if f.is_file() and f.name in names and "CMakeFiles" not in f.parts]
    if not found:
        fail(f"Gebautes Modul von '{t}' nicht gefunden (gesucht: {', '.join(names)} in {a.roots})")
    best = max(found, key=lambda f: f.stat().st_mtime)
    print(f"Modul: {best}")
    out(path=str(best.resolve()))


def read_zip_manifest(data, name):
    """manifest.json eines Plugin-ZIPs (Wurzel oder genau ein Unterordner) → dict."""
    entries = zip_entries(data, name)
    if "manifest.json" not in entries:
        fail(f"{name}: keine manifest.json im Paket")
    return load_json_lenient(entries["manifest.json"], name)


def zip_entries(data, name):
    """Dateien eines Plugin-ZIPs relativ zum Ordner mit manifest.json (Backslash → /)."""
    import io
    try:
        z = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile:
        fail(f"{name}: keine gültige ZIP-Datei")
    files = {i.filename.replace("\\", "/"): z.read(i) for i in z.infolist() if not i.filename.endswith(("/", "\\"))}
    prefix = ""
    if "manifest.json" not in files:
        found = [k for k in files if k.count("/") == 1 and k.endswith("/manifest.json")]
        if len(found) != 1:
            fail(f"{name}: keine manifest.json im Paket")
        prefix = found[0][: -len("manifest.json")]
    out_files = {}
    for k, v in files.items():
        if not k.startswith(prefix):
            continue
        rel = k[len(prefix):]
        pp = PurePosixPath(rel)
        if pp.is_absolute() or ".." in pp.parts or ":" in rel:
            fail(f"{name}: ungültiger Pfad {k}")
        out_files[rel] = v
    return out_files


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


def version_tuple(v):
    nums = [int(x) for x in re.findall(r"\d+", v.split("-")[0].split("+")[0])[:3]]
    return tuple(nums + [0] * (3 - len(nums)))


def cmd_assemble(a):
    targets = a.targets.split(",")
    built = a.built.split(",") if a.built else list(targets)
    parts = Path(a.parts)
    trees = {}
    for key in built:
        d = parts / key
        if not d.is_dir() or not any(d.rglob("*")):
            fail(f"Build für {key} fehlt (kein Modul aus dem Plattform-Job) – kein Plugin-ZIP")
        trees[key] = files_of(d)

    def common(tree):
        return {k: v for k, v in tree.items() if not k.startswith("bin/")}

    def same(x, y):   # Zeilenenden zählen nicht (Git unter Windows kann CRLF auschecken)
        return x.replace(b"\r\n", b"\n") == y.replace(b"\r\n", b"\n")

    if a.base_zip:
        first, shared, built_manifest = None, {}, {}
    # Gemeinsame Dateien der Builds: Linux (LF wie im Repository), sonst der erste
    first = None if a.base_zip else next((k for k in built if k.startswith("linux-")), built[0])
    shared = {k: v.read_bytes() for k, v in common(trees[first]).items()} if first else {}
    if first and "manifest.json" not in shared:
        fail(f"Build {first}: manifest.json fehlt im Paketordner")
    for key in [k for k in built if first and k != first]:
        other = common(trees[key])
        if set(other) != set(shared):
            fail(f"Gemeinsame Dateien von {key} und {first} unterscheiden sich: {sorted(set(other) ^ set(shared))}")
        for rel in shared:
            if not rel.endswith(".qm") and not same(shared[rel], other[rel].read_bytes()):
                fail(f"{rel} unterscheidet sich zwischen {first} und {key} – unterschiedlicher Quellstand?")
    if first:
        built_manifest = json.loads(shared["manifest.json"])
        libs = check_libraries(built_manifest, targets, "manifest.json (Quellstand)")

    if a.base_zip:
        # Einfügen: hochgeladenes Windows-ZIP bleibt, wie es ist; die Module der anderen Systeme kommen neben die DLL
        base = zip_entries(Path(a.base_zip).read_bytes(), Path(a.base_zip).name)
        manifest = load_json_lenient(base["manifest.json"], Path(a.base_zip).name)
        libs = json.loads(a.libraries)
        win_keys = [k for k in targets if k not in built]
        if len(win_keys) != 1 or not win_keys[0].startswith("windows-"):
            fail(f"Einfügen erwartet genau ein Windows-Ziel aus dem ZIP, nicht {win_keys}")
        if libs[win_keys[0]] not in base:
            fail(f"{Path(a.base_zip).name}: Windows-Modul '{libs[win_keys[0]]}' fehlt im Paket")
        tmpdll = Path(a.out) / "_win_module"
        tmpdll.parent.mkdir(parents=True, exist_ok=True)
        tmpdll.write_bytes(base[libs[win_keys[0]]])
        cmd_arch(argparse.Namespace(module=str(tmpdll), key=win_keys[0]))
        tmpdll.unlink()
        files = {k: v for k, v in base.items() if k != "manifest.json"}
        for key in built:
            mods = [f for f in trees[key].values()]
            if len(mods) != 1:
                fail(f"Build {key}: genau ein Modul erwartet, gefunden {[m.name for m in mods]}")
            if libs[key] in files:
                fail(f"{libs[key]} liegt schon im ZIP")
            files[libs[key]] = mods[0].read_bytes()
        new = {}
        for k, v in manifest.items():
            new[k] = v
            if k == "library":   # bleibt für Player vor 1.2.9 (Windows), dahinter die Auswahl je System
                new["libraries"] = {t: libs[t] for t in targets}
        plats = list(dict.fromkeys((manifest.get("supportedPlatforms") or []) + [t.split("-", 1)[0] for t in targets]))
        new["supportedPlatforms"] = sorted(plats, key=lambda x: ["windows", "linux", "macos"].index(x) if x in ("windows", "linux", "macos") else 9)
        manifest = new
    else:
        manifest = built_manifest
        files = dict(shared)
        files.pop("manifest.json")

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

    for key in built if not a.base_zip else []:
        lib = libs[key]
        own = {k: v for k, v in trees[key].items() if k.startswith("bin/")}
        if lib not in own:
            fail(f"Build {key}: {lib} fehlt (gebaut: {', '.join(own) or 'nichts'})")
        folder = PurePosixPath(lib).parent.as_posix() + "/"
        stray = [k for k in own if not k.startswith(folder)]
        if stray:
            fail(f"Build {key}: Dateien außerhalb von {folder}: {stray}")
        files.update({k: v.read_bytes() for k, v in own.items()})   # Modul und private Begleitbibliotheken

    manifest_bytes = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf8") if a.base_zip else shared["manifest.json"]
    zip_path = Path(a.out) / (a.zip_name or f"{a.package}-{a.version}.zip")
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel, data in [("manifest.json", manifest_bytes)] + sorted(files.items()):
            info = zipfile.ZipInfo(rel, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)

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
    out(zip=str(zip_path), name=zip_path.stem, file=zip_path.name, sha256=digest)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("resolve")
    r.add_argument("--id", default="")
    r.add_argument("--version", default="")
    r.add_argument("--source-ref", default="")
    r.add_argument("--player-ref", default="")
    r.add_argument("--zip-file", default="")
    v = sub.add_parser("set-version")
    v.add_argument("--manifest", required=True)
    v.add_argument("--version", required=True)
    c = sub.add_parser("arch")
    c.add_argument("--module", required=True)
    c.add_argument("--key", required=True)
    s = sub.add_parser("assemble")
    for arg in ("--parts", "--id", "--version", "--package", "--targets", "--out"):
        s.add_argument(arg, required=True)
    s.add_argument("--built", default="", help="tatsächlich gebaute Ziele (Standard: alle)")
    s.add_argument("--base-zip", default="", help="hochgeladenes Windows-ZIP, in das Linux/macOS eingefügt werden")
    s.add_argument("--zip-name", default="")
    s.add_argument("--libraries", default="{}", help="Zielschlüssel → Pfad im Paket (aus resolve)")
    l = sub.add_parser("locate")
    l.add_argument("--target", required=True)
    l.add_argument("--key", required=True)
    l.add_argument("--roots", required=True)
    a = ap.parse_args()
    {"resolve": cmd_resolve, "set-version": cmd_set_version, "locate": cmd_locate, "arch": cmd_arch, "assemble": cmd_assemble}[a.cmd](a)


if __name__ == "__main__":
    main()
