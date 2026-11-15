#!/usr/bin/env python3
"""Conqueror Engine -> Web (WebAssembly + WebGL 2) paketleyici.

Derlenmiş bir oyun klasörünü (editörde Build ile üretilen klasör: .cqproj, Assets/, Resources/, Engine/src/Shaders ...)
tarayıcıda açılan bir web sitesine dönüştürür (Unity Web derlemesi gibi):

  <çıktı>/index.html                 sayfa (Player Settings > Web > Template: Default / Minimal / Full Window)
  <çıktı>/Build/conqueror.js|.wasm   oyuncu (Emscripten ile derlenmiş motor)
  <çıktı>/Build/game.data|.json      oyun dosyaları (açılışta tarayıcı belleğindeki dosya sistemine yazılır)
  <çıktı>/coi-serviceworker.js       çok iş parçacıklı oyuncu için COOP / COEP (GitHub Pages, itch.io ...)

  python3 Tools/Web/package_web.py <oyun-klasörü> --out <klasör> [--runtime <conqueror.js klasörü>] [--build]
                                   [--threads | --no-threads] [--compression none|gzip|brotli] [--serve [PORT]]

Oyuncu bir kez derlenir (--build: Emscripten SDK gerekir, $EMSDK ya da ~/.cache/emsdk ya da ~/emsdk); paketleme yalnız Python ister.
Sıkıştırma: gzip her sunucuda çalışır (tarayıcı açar); brotli için sunucu .br dosyalarını "Content-Encoding: br" ile vermelidir
(Tools/Web/serve.py, Netlify, Cloudflare ...). Yerel deneme: python3 Tools/Web/serve.py <çıktı> (file:// ile açılmaz).
"""

import argparse
import gzip
import hashlib
import html
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[2]
TEMPLATE_DIR = Path(__file__).resolve().parent / "template"
# Brotli kalitesi: 9 hızlı ve 11'e yakın boyut (11, 100 MB veride dakikalar sürer); CQ_WEB_BROTLI_QUALITY ile değişir
BROTLI_QUALITY = max(1, min(11, int(os.environ.get("CQ_WEB_BROTLI_QUALITY", "9") or 9)))


def log(msg):
    print(f"[package_web] {msg}", flush=True)


def fail(msg):
    print(f"[package_web] ERROR: {msg}", file=sys.stderr, flush=True)
    sys.exit(1)


def read_project(game_dir):
    """(.cqproj yolu, ad, sürüm, Web ayarları, oyuncu genişliği / yüksekliği) — basit YAML okuması (bağımlılık yok)."""
    projects = sorted(game_dir.glob("*.cqproj"))
    if not projects:
        fail(f"no .cqproj in {game_dir} (build the game in the editor first, or pass the project folder)")
    text = projects[0].read_text(encoding="utf-8", errors="replace")

    def scalar(key, default, block=text):
        m = re.search(rf"^\s*{key}:\s*(.+?)\s*$", block, re.M)
        return (m.group(1).strip().strip("'\"") if m else default) or default

    web = {}
    m = re.search(r"^(\s*)Web:\s*$", text, re.M)
    if m:
        indent = len(m.group(1))
        lines = []
        for line in text[m.end():].splitlines()[1:]:
            if line.strip() and len(line) - len(line.lstrip()) <= indent:
                break
            lines.append(line)
        block = "\n".join(lines)
        for key in ("GraphicsAPI", "Template", "Compression", "MemoryMB", "Threads", "DataCaching"):
            v = scalar(key, None, block)
            if v is not None:
                web[key] = v
    player = re.search(r"^(\s*)Player:\s*$", text, re.M)
    width, height = 1280, 720
    if player:
        block = text[player.end():]
        w = re.search(r"^\s*Width:\s*(\d+)", block, re.M)
        h = re.search(r"^\s*Height:\s*(\d+)", block, re.M)
        width, height = (int(w.group(1)) if w else 1280), (int(h.group(1)) if h else 720)
    return projects[0], scalar("Name", projects[0].stem), scalar("Version", "1.0"), web, width, height


def is_desktop_binary(path):
    if path.suffix.lower() in (".exe", ".dll", ".so", ".dylib", ".pdb", ".wasm"):
        return True
    if path.suffix == "" and path.is_file():
        with open(path, "rb") as f:
            return f.read(4) == b"\x7fELF"
    return False


def collect_game_files(game_dir):
    """Oyun klasöründeki paketlenecek dosyalar (masaüstü ikilileri, lib/ ve önceki web çıktısı hariç)."""
    files = []
    for root, dirs, names in os.walk(game_dir):
        rel_root = Path(root).relative_to(game_dir)
        if rel_root == Path("."):
            dirs[:] = [d for d in dirs if d not in ("lib", "Library", "Logs", "Temp", ".git", "Web", "Build", "Cache")]
        dirs.sort()
        for n in sorted(names):
            p = Path(root) / n
            if rel_root == Path(".") and (is_desktop_binary(p) or n in ("index.html", "coi-serviceworker.js")):
                continue
            if n in ("imgui.ini",) or n.endswith((".log", ".tmp")):
                continue
            files.append(p.relative_to(game_dir).as_posix())
    return files


def find_emsdk():
    for c in (os.environ.get("EMSDK"), Path.home() / ".cache" / "emsdk", Path.home() / "emsdk"):
        if c and (Path(c) / "emsdk_env.sh").is_file():
            return Path(c)
    return None


def build_jobs():
    """Paralel derleme sayısı: CQ_WEB_JOBS ya da çekirdek sayısı, boş belleğe göre sınırlı (her Emscripten derleyicisi ~1,2 GB)."""
    if os.environ.get("CQ_WEB_JOBS", "").isdigit():
        return max(1, int(os.environ["CQ_WEB_JOBS"]))
    jobs = os.cpu_count() or 2
    try:
        with open("/proc/meminfo", encoding="ascii") as f:
            info = {line.split(":")[0]: int(line.split()[1]) for line in f if ":" in line}
        jobs = min(jobs, max(1, int(info.get("MemAvailable", 0) / (1.2 * 1024 * 1024))))
    except (OSError, ValueError):
        pass
    return max(1, jobs)


def build_runtime(build_root, threads):
    """Oyuncuyu Emscripten ile derler (<build_root>/web[-mt]/bin/conqueror.js + .wasm)."""
    emsdk = find_emsdk()
    if not emsdk:
        fail("Emscripten SDK not found: install it (https://emscripten.org/docs/getting_started/downloads.html) "
             "and set EMSDK, or pass --runtime with a prebuilt player")
    out = build_root / ("web-mt" if threads else "web")
    env_sh = emsdk / "emsdk_env.sh"
    flags = f"-DCQ_WEB_THREADS={'ON' if threads else 'OFF'}"
    script = (f'source "{env_sh}" >/dev/null 2>&1 && '
              f'emcmake cmake -S "{ENGINE_ROOT}" -B "{out}" -G Ninja -DCMAKE_BUILD_TYPE=Release {flags} && '
              f'cmake --build "{out}" --target ConquerorRuntime --parallel {build_jobs()}')
    log(f"building the web player ({'threads' if threads else 'single thread'}) in {out}")
    if subprocess.call(["bash", "-c", script]) != 0:
        fail("web player build failed")
    return out / "bin"


def find_runtime(explicit, build_root, threads):
    name = "web-mt" if threads else "web"
    candidates = [Path(explicit)] if explicit else []
    if os.environ.get("CQ_WEB_RUNTIME"):
        candidates.append(Path(os.environ["CQ_WEB_RUNTIME"]))
    candidates += [build_root / name / "bin", ENGINE_ROOT / f"build-{name}" / "bin", ENGINE_ROOT / "Runtime" / "Web" / name]
    for c in candidates:
        if (c / "conqueror.js").is_file() and (c / "conqueror.wasm").is_file():
            return c
    return None


def compress(path, method):
    """Dosyayı sıkıştırır; çıktı adını döndürür (method none ise aynısı)."""
    if method == "gzip":
        out = path.with_name(path.name + ".gz")
        with open(path, "rb") as src, open(out, "wb") as raw, \
                gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0) as dst:
            shutil.copyfileobj(src, dst, 1 << 20)
        path.unlink()
        return out
    if method == "brotli":
        out = path.with_name(path.name + ".br")
        try:
            import brotli  # type: ignore
            out.write_bytes(brotli.compress(path.read_bytes(), quality=BROTLI_QUALITY))
        except ImportError:
            tool = shutil.which("brotli")
            if not tool:
                log("brotli is not installed (pip install brotli or the brotli tool): using gzip instead")
                return compress(path, "gzip")
            subprocess.check_call([tool, "-q", str(BROTLI_QUALITY), "-f", "-o", str(out), str(path)])
        path.unlink()
        return out
    return path


def write_data(game_dir, files, build_dir):
    """Oyun dosyalarını tek veri dosyasına yazar; manifest: klasörler + (yol, konum, boyut)."""
    entries = []
    dirs = set()
    offset = 0
    digest = hashlib.sha256()
    with open(build_dir / "game.data", "wb") as out:
        for rel in files:
            data = (game_dir / rel).read_bytes()
            out.write(data)
            digest.update(rel.encode("utf-8") + b"\0" + data)
            entries.append([rel, offset, len(data)])
            offset += len(data)
            parent = Path(rel).parent
            while parent != Path("."):
                dirs.add(parent.as_posix())
                parent = parent.parent
    manifest = {"dirs": sorted(dirs, key=lambda d: (d.count("/"), d)), "files": entries, "size": offset,
                "hash": digest.hexdigest()[:16]}
    return manifest


def main():
    ap = argparse.ArgumentParser(description="Package a built Conqueror game for the web (WebAssembly + WebGL 2)")
    ap.add_argument("game", help="built game folder (or project folder)")
    ap.add_argument("--out", required=True, help="output folder (a static web site)")
    ap.add_argument("--runtime", help="folder with conqueror.js + conqueror.wasm (prebuilt web player)")
    ap.add_argument("--build", action="store_true", help="build the web player with Emscripten first")
    ap.add_argument("--build-root", default=os.environ.get("CQ_WEB_BUILD_ROOT", str(Path.home() / ".cache" / "cq-build")))
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--threads", dest="threads", action="store_true", default=None)
    g.add_argument("--no-threads", dest="threads", action="store_false")
    ap.add_argument("--compression", choices=["none", "gzip", "brotli"])
    ap.add_argument("--graphics-api", choices=["webgl2", "webgpu"], help="override Player Settings > Web > Graphics API")
    ap.add_argument("--title", help="page title (default: the game name)")
    ap.add_argument("--serve", nargs="?", const=8080, type=int, metavar="PORT",
                    help="serve the result locally after packaging (COOP / COEP headers)")
    args = ap.parse_args()

    game_dir = Path(args.game).resolve()
    if not game_dir.is_dir():
        fail(f"{game_dir} is not a folder")
    _, name, version, web, width, height = read_project(game_dir)
    threads = args.threads if args.threads is not None else web.get("Threads", "false").lower() in ("true", "1", "yes")
    comp_idx = {"0": "none", "1": "gzip", "2": "brotli"}
    compression = args.compression or comp_idx.get(str(web.get("Compression", "1")), "gzip")
    template = {"0": "default", "1": "minimal", "2": "fullwindow"}.get(str(web.get("Template", "0")), "default")
    caching = web.get("DataCaching", "true").lower() in ("true", "1", "yes")
    memory_mb = max(32, min(4096, int(web.get("MemoryMB", "256") or 256)))

    build_root = Path(args.build_root)
    runtime = build_runtime(build_root, threads) if args.build else find_runtime(args.runtime, build_root, threads)
    if runtime is None:
        fail("web player (conqueror.js + conqueror.wasm) not found: run with --build (needs the Emscripten SDK) "
             "or pass --runtime")

    out = Path(args.out).resolve()
    if out == game_dir or (game_dir in out.parents and out.relative_to(game_dir).parts[0] not in ("Web", "Build")):
        fail("the output folder must not be inside the game folder (except its Web/ or Build/ folder)")
    build_dir = out / "Build"
    if build_dir.is_dir():
        # yalnız önceki web paketinin klasörü silinir (başka bir şeyin Build klasörü değil)
        if not ((build_dir / "game.json").is_file() or (build_dir / "conqueror.js").is_file()):
            fail(f"{build_dir} exists and is not a previous web build: choose another output folder")
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True)

    files = collect_game_files(game_dir)
    if not files:
        fail("no game files found")
    log(f"{name} {version}: {len(files)} files, {'threads' if threads else 'single thread'}, compression {compression}")
    manifest = write_data(game_dir, files, build_dir)

    # Player Settings > Web > Graphics API: WebGPU (oyuncu WebGPU arka ucuyla derlendiyse; tarayıcıda yoksa WebGL 2'ye düşülür)
    framework = (runtime / "conqueror.js").read_text(encoding="utf-8", errors="replace")
    runtime_webgpu = "preinitializedWebGPUDevice" in framework
    want_webgpu = args.graphics_api == "webgpu" if args.graphics_api else str(web.get("GraphicsAPI", "0")) == "1"
    if want_webgpu and not runtime_webgpu:
        log("Graphics API WebGPU is selected but this web player was built without the WebGPU backend (needs CQ_DAWN_DIR): using WebGL 2")
    shutil.copyfile(runtime / "conqueror.js", build_dir / "conqueror.js")
    wasm = build_dir / "conqueror.wasm"
    shutil.copyfile(runtime / "conqueror.wasm", wasm)
    def file_hash(path):
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        return h.hexdigest()[:16]
    # Önbellek anahtarları dosya başına: oyuncu ya da veri değişince tarayıcı eskisini kullanmaz
    wasm_hash = file_hash(wasm)
    data_name = compress(build_dir / "game.data", compression).name
    wasm_name = compress(wasm, compression).name
    (build_dir / "game.json").write_text(json.dumps(manifest, separators=(",", ":")), encoding="utf-8")

    config = {
        "name": name, "version": version, "dataUrl": "Build/" + data_name, "manifestUrl": "Build/game.json",
        "wasmUrl": "Build/" + wasm_name, "frameworkUrl": "Build/conqueror.js", "threads": threads,
        "webgpu": want_webgpu and runtime_webgpu, "caching": caching, "dataHash": manifest["hash"], "wasmHash": wasm_hash, "width": width, "height": height, "memoryMB": memory_mb,
    }
    page = (TEMPLATE_DIR / f"{template}.html").read_text(encoding="utf-8")
    loader = (TEMPLATE_DIR / "loader.js").read_text(encoding="utf-8")
    page = (page.replace("{{TITLE}}", html.escape(args.title or name))
                .replace("{{WIDTH}}", str(width)).replace("{{HEIGHT}}", str(height))
                .replace("{{CONFIG}}", json.dumps(config))
                .replace("{{LOADER}}", loader))
    (out / "index.html").write_text(page, encoding="utf-8")
    if threads:
        shutil.copyfile(TEMPLATE_DIR / "coi-serviceworker.js", out / "coi-serviceworker.js")
    elif (out / "coi-serviceworker.js").exists():
        (out / "coi-serviceworker.js").unlink()
    total = sum(p.stat().st_size for p in build_dir.iterdir())
    log(f"done: {out} ({total / 1048576:.1f} MB). Open it through a web server: python3 {ENGINE_ROOT / 'Tools/Web/serve.py'} \"{out}\"")
    if args.serve:
        os.execv(sys.executable, [sys.executable, str(Path(__file__).resolve().parent / "serve.py"), str(out),
                                  "--port", str(args.serve)])


if __name__ == "__main__":
    main()
