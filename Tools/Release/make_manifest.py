#!/usr/bin/env python3
"""conqueror-release.json (47): dist/ klasöründeki editör ve modül arşivlerinden (package.py'nin .meta.json dosyaları) Hub'ın okuduğu
sürüm bildirimi. Boyut ve SHA-256 burada hesaplanır.

usage: make_manifest.py --dist dist --channel official|prerelease|conquerorx [--commit SHA] [--version V]
"""
import argparse
import glob
import hashlib
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def engine_version():
    text = open(os.path.join(ROOT, "Engine", "src", "Core", "Base", "Version.h"), encoding="utf-8").read()
    return re.search(r'#define CQ_ENGINE_VERSION "([^"]+)"', text).group(1)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dist", required=True)
    ap.add_argument("--channel", default="official")
    ap.add_argument("--commit", default=os.environ.get("GITHUB_SHA", ""))
    ap.add_argument("--version", default="")
    args = ap.parse_args()
    manifest = {"version": args.version or engine_version(), "commit": args.commit, "channel": args.channel, "editors": {}, "modules": []}
    modules = {}
    for meta_path in sorted(glob.glob(os.path.join(args.dist, "**", "*.meta.json"), recursive=True)):
        archive = meta_path[: -len(".meta.json")]
        if not os.path.exists(archive):
            continue
        meta = json.load(open(meta_path))
        entry = {"asset": os.path.basename(archive), "size": os.path.getsize(archive), "sha256": sha256(archive),
                 "installed_size": meta.get("installed_size", 0)}
        if meta["kind"] == "module":
            entry["bundles"] = meta.get("bundles", [])
        if meta["kind"] == "editor":
            manifest["editors"][meta["host"]] = entry
        else:
            m = modules.setdefault(meta["id"], {"id": meta["id"], "assets": {}, "bundles": []})
            m["assets"][meta["host"]] = entry
            for b in meta.get("bundles", []):
                if b not in m["bundles"]:
                    m["bundles"].append(b)
    manifest["modules"] = [modules[k] for k in sorted(modules)]
    out = os.path.join(args.dist, "conqueror-release.json")
    with open(out, "w") as f:
        json.dump(manifest, f, indent=1)
    print(f"{out}: {len(manifest['editors'])} editor(s), {len(manifest['modules'])} module(s)")


if __name__ == "__main__":
    main()
