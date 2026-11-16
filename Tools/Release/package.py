#!/usr/bin/env python3
"""Sürüm paketleri (47): Hub'ın indirdiği editör ve modül arşivleri (.tar.gz). Her arşivin yanına make_manifest.py'nin okuduğu
bir .meta.json yazılır.

usage:
  package.py editor --bin build/bin --host linux-x86_64 --out dist [--channel official|prerelease|conquerorx] [--commit SHA]
  package.py module <id> --host any|<host> --out dist --add <kaynak>[=<arşivdeki yol>] ... [--bundles jdk,dotnet]

Editör arşivi: build/bin'in içeriği (günlükler, önbellekler, test projeleri hariç) + conqueror-install.json (sürüm, commit, kanal).
Modül arşivi: Hub onu <kurulum>/Modules/<id>/ içine açar; --add ile verilen dosya / klasörler arşivin köküne (ya da =yol'a) girer.
"""
import argparse
import json
import os
import re
import sys
import tarfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SKIP_DIRS = {"Library", "Logs", "logs", "Temp", "regression", "__pycache__", ".cache", "Cache", "Modules"}
# Günlükler, hata ayıklama dosyaları, yerel önbellek / kara kutu; NVIDIA DLSS çalışma zamanı ASLA dağıtılmaz (ayrı lisans)
SKIP_FILES = re.compile(r"(\.log|\.pdb|\.ilk|\.exp|\.lib|\.a|imgui\.ini|\.result\.txt|blackbox\.bin|\.log\.\d+)$|^libnvidia-ngx|^nvngx", re.I)


def engine_version():
    text = open(os.path.join(ROOT, "Engine", "src", "Core", "Base", "Version.h"), encoding="utf-8").read()
    return re.search(r'#define CQ_ENGINE_VERSION "([^"]+)"', text).group(1)


def tar_filter(info):
    # Herkes okuyabilir, çalıştırılabilirler çalıştırılabilir kalır; sahiplik sıfır (tekrarlanabilir arşiv)
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    info.mtime = 0
    return info


NEVER = re.compile(r"^(libnvidia-ngx|nvngx)", re.I)   # NVIDIA DLSS: her durumda dışarıda


def add_tree(tar, src, arc, skip=True):
    if NEVER.search(os.path.basename(src)):
        return 0
    if os.path.isfile(src) or os.path.islink(src):
        tar.add(src, arcname=arc, recursive=False, filter=tar_filter)
        return os.path.getsize(src) if os.path.isfile(src) else 0
    total = 0
    for dirpath, dirnames, filenames in os.walk(src):
        dirnames[:] = sorted(d for d in dirnames if not (skip and d in SKIP_DIRS))
        for name in sorted(filenames):
            if (skip and SKIP_FILES.search(name)) or NEVER.search(name):
                continue
            p = os.path.join(dirpath, name)
            rel = os.path.relpath(p, src)
            tar.add(p, arcname=os.path.join(arc, rel) if arc else rel, recursive=False, filter=tar_filter)
            if os.path.isfile(p) and not os.path.islink(p):
                total += os.path.getsize(p)
    return total


def write_meta(archive, meta):
    meta["installed_size"] = meta.get("installed_size", 0)
    with open(archive + ".meta.json", "w") as f:
        json.dump(meta, f, indent=1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kind", choices=["editor", "module"])
    ap.add_argument("id", nargs="?")
    ap.add_argument("--bin")
    ap.add_argument("--lib", help="shared libraries for lib/ (only .so / .dylib files are taken)")
    ap.add_argument("--host", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--channel", default="official")
    ap.add_argument("--commit", default=os.environ.get("GITHUB_SHA", ""))
    ap.add_argument("--add", action="append", default=[])
    ap.add_argument("--bundles", default="")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    version = engine_version()

    if args.kind == "editor":
        tag = f"conquerorx-{args.commit[:8]}" if args.channel == "conquerorx" else version
        name = f"ConquerorEngine-{tag}-{args.host}.tar.gz"
        path = os.path.join(args.out, name)
        info = json.dumps({"version": version, "commit": args.commit, "channel": args.channel}, indent=1).encode()
        with tarfile.open(path, "w:gz", compresslevel=9, format=tarfile.GNU_FORMAT) as tar:
            # Düzen: bin/ (editör, Hub, oyuncu; RUNPATH $ORIGIN/../lib) + lib/ (paylaşımlı kitaplıklar)
            size = add_tree(tar, args.bin, "bin")
            if args.lib and os.path.isdir(args.lib):
                for lib in sorted(os.listdir(args.lib)):
                    p = os.path.join(args.lib, lib)
                    if (".so" in lib or lib.endswith(".dylib")) and not SKIP_FILES.search(lib) and (os.path.isfile(p) or os.path.islink(p)):
                        size += add_tree(tar, p, os.path.join("lib", lib))
            # Paketleyiciler (Build, betikleri editörün klasöründen yukarı doğru arar): Tools/<platform>
            for tool in ("Android", "Web", "iOS", "HarmonyOS", "Linux", "Windows", "macOS", "QNX", "Hurd", "FFmpeg"):
                src = os.path.join(ROOT, "Tools", tool)
                if os.path.isdir(src):
                    size += add_tree(tar, src, os.path.join("Tools", tool))
            ti = tarfile.TarInfo("conqueror-install.json")
            ti.size = len(info)
            ti.mode = 0o644
            import io
            tar.addfile(ti, io.BytesIO(info))
        write_meta(path, {"kind": "editor", "host": args.host, "installed_size": size})
    else:
        if not args.id:
            sys.exit("module id missing")
        name = f"module-{args.id}-{args.host}.tar.gz"
        path = os.path.join(args.out, name)
        size = 0
        with tarfile.open(path, "w:gz", compresslevel=9, format=tarfile.GNU_FORMAT) as tar:
            for spec in args.add:
                src, _, arc = spec.partition("=")
                if not os.path.exists(src):
                    sys.exit(f"missing: {src}")
                size += add_tree(tar, src, arc or os.path.basename(src.rstrip("/")), skip=False)
        bundles = [b for b in args.bundles.split(",") if b]
        write_meta(path, {"kind": "module", "id": args.id, "host": args.host, "bundles": bundles, "installed_size": size})
    print(f"{name}: {os.path.getsize(path) / 1048576:.1f} MB")


if __name__ == "__main__":
    main()
