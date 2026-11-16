#!/usr/bin/env python3
"""Dil modüllerinin içinde gelen araç zincirlerini indirir (47; yalnız lisansı yeniden dağıtıma izin verenler): Zig (MIT),
Eclipse Temurin JDK 21 (GPL-2.0 + Classpath Exception), .NET 8 SDK (MIT), llvm-mingw (Apache-2.0 + LLVM exception, Windows C/C++).
Her dosyanın sağlama toplamı yayıncının kendi kaydından doğrulanır; tutmazsa çıkış kodu 1.

usage: fetch_toolchain.py <zig|jdk|dotnet|llvm-mingw> <host: linux-x86_64|linux-arm64|windows-x86_64|macos-arm64> <hedef klasör>
"""
import hashlib
import io
import json
import os
import shutil
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

UA = {"User-Agent": "conqueror-release"}


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=600) as r:
        return r.read()


def version_key(v):
    return [int(p) if p.isdigit() else 0 for p in v.replace("-", ".").split(".")]


def verify(data, algo, expected):
    h = hashlib.new(algo, data).hexdigest()
    if h.lower() != expected.lower():
        sys.exit(f"checksum mismatch ({algo}): got {h}, expected {expected}")


def extract(data, name, dest, strip_to=None):
    tmp = tempfile.mkdtemp()
    try:
        if name.endswith(".zip"):
            zipfile.ZipFile(io.BytesIO(data)).extractall(tmp)
        else:
            with tarfile.open(fileobj=io.BytesIO(data)) as t:
                t.extractall(tmp, filter="tar")
        root = tmp
        entries = os.listdir(root)
        if len(entries) == 1 and os.path.isdir(os.path.join(root, entries[0])):
            root = os.path.join(root, entries[0])     # tek üst klasör atlanır
        if strip_to and os.path.isdir(os.path.join(root, strip_to)):
            root = os.path.join(root, strip_to)       # macOS JDK: Contents/Home
        if os.path.isdir(dest):
            shutil.rmtree(dest)
        shutil.copytree(root, dest, symlinks=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def zig(host, dest):
    index = json.loads(get("https://ziglang.org/download/index.json"))
    version = max((k for k in index if k != "master"), key=version_key)
    key = {"linux-x86_64": "x86_64-linux", "linux-arm64": "aarch64-linux", "windows-x86_64": "x86_64-windows", "macos-arm64": "aarch64-macos"}[host]
    entry = index[version][key]
    data = get(entry["tarball"])
    verify(data, "sha256", entry["shasum"])
    extract(data, entry["tarball"], dest)
    return f"Zig {version}"


def jdk(host, dest):
    osname, arch = {"linux-x86_64": ("linux", "x64"), "linux-arm64": ("linux", "aarch64"), "windows-x86_64": ("windows", "x64"),
                    "macos-arm64": ("mac", "aarch64")}[host]
    info = json.loads(get(f"https://api.adoptium.net/v3/assets/latest/21/hotspot?architecture={arch}&image_type=jdk&os={osname}&vendor=eclipse"))
    pkg = info[0]["binary"]["package"]
    data = get(pkg["link"])
    verify(data, "sha256", pkg["checksum"])
    extract(data, pkg["name"], dest, strip_to=os.path.join("Contents", "Home") if osname == "mac" else None)
    return f"Temurin JDK {info[0]['version']['semver']}"


def dotnet(host, dest):
    meta = json.loads(get("https://dotnetcli.blob.core.windows.net/dotnet/release-metadata/8.0/releases.json"))
    latest = meta["latest-sdk"]
    rid = {"linux-x86_64": "linux-x64", "linux-arm64": "linux-arm64", "windows-x86_64": "win-x64", "macos-arm64": "osx-arm64"}[host]
    for rel in meta["releases"]:
        sdk = rel.get("sdk") or {}
        if sdk.get("version") != latest:
            continue
        for f in sdk["files"]:
            if f.get("rid") == rid and (f["name"].endswith(".zip") if rid.startswith("win") else f["name"].endswith(".tar.gz")):
                data = get(f["url"])
                verify(data, "sha512", f["hash"])
                os.makedirs(dest, exist_ok=True)
                tmp = dest + ".tmp"
                extract(data, f["name"], tmp)
                if os.path.isdir(dest):
                    shutil.rmtree(dest)
                os.rename(tmp, dest)
                return f".NET SDK {latest}"
    sys.exit(f".NET SDK {latest} for {rid} not found")


def llvm_mingw(host, dest):
    rel = json.loads(get("https://api.github.com/repos/mstorsjo/llvm-mingw/releases/latest"))
    for a in rel["assets"]:
        if a["name"].endswith("ucrt-x86_64.zip"):
            digest = a.get("digest") or ""
            if not digest.startswith("sha256:"):
                sys.exit("llvm-mingw asset has no SHA-256 digest; refusing to bundle an unverified download")
            data = get(a["browser_download_url"])
            verify(data, "sha256", digest.split(":", 1)[1])
            extract(data, a["name"], dest)
            return f"llvm-mingw {rel['tag_name']}"
    sys.exit("llvm-mingw ucrt-x86_64 zip not found")


def bundletool(host, dest):
    rel = json.loads(get("https://api.github.com/repos/google/bundletool/releases/latest"))
    for a in rel["assets"]:
        if a["name"].startswith("bundletool-all-") and a["name"].endswith(".jar"):
            digest = a.get("digest") or ""
            if not digest.startswith("sha256:"):
                sys.exit("bundletool asset has no SHA-256 digest; refusing to bundle an unverified download")
            data = get(a["browser_download_url"])
            verify(data, "sha256", digest.split(":", 1)[1])
            os.makedirs(dest, exist_ok=True)
            with open(os.path.join(dest, "bundletool.jar"), "wb") as f:
                f.write(data)
            return f"bundletool {rel['tag_name']} (Apache-2.0)"
    sys.exit("bundletool jar not found")


def main():
    tool, host, dest = sys.argv[1], sys.argv[2], os.path.abspath(sys.argv[3])
    what = {"zig": zig, "jdk": jdk, "dotnet": dotnet, "llvm-mingw": llvm_mingw, "bundletool": bundletool}[tool](host, dest)
    with open(os.path.join(dest, "BUNDLED.txt"), "w") as f:
        f.write(f"{what} for {host}, bundled by Conqueror's Engine (see the tool's own license file in this folder).\n")
    print(f"{tool}: {what} -> {dest}")


if __name__ == "__main__":
    main()
