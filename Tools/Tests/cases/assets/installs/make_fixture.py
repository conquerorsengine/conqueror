#!/usr/bin/env python3
"""Hub > Installs testi (47) için sahte GitHub Releases düzeneği: GitHub API biçiminde releases.json, sürüm bildirimleri
(conqueror-release.json), editör ve modül arşivleri (.tar.gz / .tar.zst), file:// adresleriyle. Ağ kullanılmaz.

usage: make_fixture.py <out dir>

Oluşanlar:
  releases.json      Official 0.575.3 (editör + android, java, web, docs [bozuk SHA-256]), Pre-release 0.577.3-rc1 (.tar.zst),
                     ConquerorX (commit aaaa1111) ve atlanması gereken bir taslak
  releases_x2.json   aynısı ama ConquerorX bbbb2222 (güncelleme testi)
  located/           Locate ile eklenecek "kaynaktan derlenmiş" editör (--version: 0.571.3)
  Proj575/           EditorVersion 0.575.3 olan bir proje
"""
import hashlib
import io
import json
import os
import shutil
import sys
import tarfile


def host_id():
    import platform
    m = platform.machine().lower()
    arch = {"amd64": "x86_64", "x86_64": "x86_64", "aarch64": "arm64", "arm64": "arm64"}.get(m, m)
    osname = {"linux": "linux", "darwin": "macos", "win32": "windows"}.get(sys.platform, sys.platform)
    return f"{osname}-{arch}"


def editor_script(version):
    return f'#!/bin/sh\nif [ "$1" = "--version" ]; then echo "Conqueror\'s Engine {version}"; exit 0; fi\necho "fake editor {version}"\n'


def add_bytes(tar, name, data, mode=0o644):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = mode
    tar.addfile(info, io.BytesIO(data))


def make_archive(path, files, links=(), zst=False):
    mode = "w:zst" if zst else "w:gz"
    with tarfile.open(path, mode, format=tarfile.GNU_FORMAT) as tar:
        for name, data, fmode in files:
            add_bytes(tar, name, data, fmode)
        for name, target in links:
            info = tarfile.TarInfo(name)
            info.type = tarfile.SYMTYPE
            info.linkname = target
            tar.addfile(info)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    out = os.path.abspath(sys.argv[1])
    if os.path.isdir(out) and os.path.basename(out) == "installs_fixture":
        shutil.rmtree(out)
    rel = os.path.join(out, "rel")
    os.makedirs(rel)
    host = host_id()
    url = lambda name: "file://" + os.path.join(rel, name)
    long_name = "Resources/" + "deep/" * 30 + "long_file_name.txt"   # > 100 karakter: GNU uzun ad başlığı

    def editor(version, commit, channel, zst=False, tag=None):
        name = f"ConquerorEngine-{tag or version}-{host}.tar." + ("zst" if zst else "gz")
        info = json.dumps({"version": version, "commit": commit, "channel": channel}).encode()
        make_archive(os.path.join(rel, name), [
            ("ConquerorEngine", editor_script(version).encode(), 0o755),
            ("conqueror-install.json", info, 0o644),
            ("Resources/readme.txt", b"fake resources\n", 0o644),
            (long_name, b"long name ok\n", 0o644),
        ], links=[("Resources/latest.txt", "readme.txt")], zst=zst)
        p = os.path.join(rel, name)
        return name, {"asset": name, "size": os.path.getsize(p), "sha256": sha256(p)}

    def module(mid, files):
        name = f"module-{mid}.tar.gz"
        make_archive(os.path.join(rel, name), files)
        p = os.path.join(rel, name)
        return name, {"asset": name, "size": os.path.getsize(p), "sha256": sha256(p), "installed_size": 4096}

    # Official 0.575.3
    ed_name, ed = editor("0.575.3", "c575c575", "official")
    mods = []
    assets = [ed_name]
    for mid, files, extra in [
        ("android", [("player/ConquerorRuntime-arm64.so", b"\x7fELF fake", 0o755)], {}),
        ("java", [("jdk/bin/javac", b"#!/bin/sh\necho javac 21\n", 0o755), ("jdk/release", b'JAVA_VERSION="21.0.2"\n', 0o644)], {"bundles": ["jdk"]}),
        ("web", [("player/conqueror.wasm", b"\0asm fake", 0o644)], {}),
        ("docs", [("docs/index.html", b"<html></html>", 0o644)], {}),
    ]:
        n, a = module(mid, files)
        if mid == "docs":
            a["sha256"] = "0" * 64   # bozuk indirme: Hub reddetmeli
        mods.append(dict({"id": mid, "assets": {"any": a}}, **extra))
        assets.append(n)
    manifest = {"version": "0.575.3", "commit": "c575c575", "editors": {host: ed}, "modules": mods}
    with open(os.path.join(rel, "manifest-0.575.3.json"), "w") as f:
        json.dump(manifest, f)

    # Pre-release 0.577.3-rc1 (.tar.zst)
    pre_name, pre = editor("0.577.3-rc1", "c577c577", "prerelease", zst=True)
    with open(os.path.join(rel, "manifest-0.577.3-rc1.json"), "w") as f:
        json.dump({"version": "0.577.3-rc1", "commit": "c577c577", "editors": {host: pre}, "modules": []}, f)

    def conquerorx(commit):
        n, e = editor("0.577.3", commit, "conquerorx", tag="conquerorx-" + commit)
        mname = f"manifest-conquerorx-{commit}.json"
        with open(os.path.join(rel, mname), "w") as f:
            json.dump({"version": "0.577.3", "commit": commit, "editors": {host: e}, "modules": []}, f)
        return n, mname

    def gh_release(tag, name, prerelease, files, manifest_file, draft=False, commit="main"):
        assets_json = [{"name": a, "size": os.path.getsize(os.path.join(rel, a)), "browser_download_url": url(a)} for a in files]
        assets_json.append({"name": "conqueror-release.json", "size": os.path.getsize(os.path.join(rel, manifest_file)),
                            "browser_download_url": url(manifest_file)})
        return {"tag_name": tag, "name": name, "draft": draft, "prerelease": prerelease, "published_at": "2026-11-16T12:00:00Z",
                "html_url": f"https://github.com/conquerorsengine/conqueror/releases/tag/{tag}", "body": "Notes é \"quoted\"\r\nline 2",
                "target_commitish": commit, "assets": assets_json}

    x1, x1m = conquerorx("aaaa1111")
    x2, x2m = conquerorx("bbbb2222")
    common = [
        gh_release("v0.577.3-rc1", "Conqueror's Engine 0.577.3 RC 1", True, [pre_name], "manifest-0.577.3-rc1.json"),
        gh_release("v0.575.3", "Conqueror's Engine 0.575.3", False, assets, "manifest-0.575.3.json"),
        gh_release("v0.579.3", "Draft", False, [ed_name], "manifest-0.575.3.json", draft=True),
    ]
    for fname, xn, xm in [("releases.json", x1, x1m), ("releases_x2.json", x2, x2m)]:
        with open(os.path.join(out, fname), "w") as f:
            json.dump([gh_release("conquerorx", "ConquerorX", True, [xn], xm)] + common, f, indent=1)

    # Locate: kaynaktan derlenmiş editör
    located = os.path.join(out, "located")
    os.makedirs(located)
    with open(os.path.join(located, "ConquerorEngine"), "w") as f:
        f.write(editor_script("0.571.3"))
    os.chmod(os.path.join(located, "ConquerorEngine"), 0o755)

    # 0.575.3 ile kaydedilmiş proje
    proj = os.path.join(out, "Proj575")
    os.makedirs(proj)
    with open(os.path.join(proj, "Proj575.cqproj"), "w") as f:
        f.write("Project:\n  Name: Proj575\n  Version: 1.0.0\n  EditorVersion: 0.575.3\n  StartScene: Assets/Scenes/Main.cqscene\n")
    os.makedirs(os.path.join(out, "root"))
    print("fixture ready:", out, "host", host)


if __name__ == "__main__":
    main()
