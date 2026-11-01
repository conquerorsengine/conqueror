#!/usr/bin/env python3
"""Conqueror Engine -> Android APK paketleyici.

Derlenmiş bir oyun klasörünü (editörde Build ile üretilen klasör: .cqproj, Assets/, Resources/, Engine/src/Shaders ...)
NativeActivity tabanlı bir APK'ya dönüştürür. Oyuncu (libconqueror.so) Android NDK ile derlenir; Java kodu yoktur.

  python3 Tools/Android/package_apk.py <oyun-klasörü> --out MyGame.apk [--abi arm64-v8a --abi x86_64] [--build]
                                       [--automotive] [--install] [--run]

Gerekenler: Android SDK (build-tools: aapt2, zipalign, apksigner; platforms/android-NN/android.jar), NDK (yalnızca
--build), JDK (keytool / apksigner). SDK yolu $ANDROID_HOME, $ANDROID_SDK_ROOT ya da ~/Android/Sdk.
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ABIS = ["arm64-v8a", "x86_64"]
MIN_SDK = 26          # Android 8.0: AAudio, EGL/GLES 3.2 yaygın
TARGET_SDK = 36
AUTOMOTIVE_TARGET_SDK = 34   # 35+ hedefleyen uygulamalar sistem çubuklarının altına çizilmeye zorlanır


def log(msg):
    print(f"[package_apk] {msg}", flush=True)


def fail(msg):
    print(f"[package_apk] ERROR: {msg}", file=sys.stderr, flush=True)
    sys.exit(1)


def run(cmd, **kw):
    log("$ " + " ".join(str(c) for c in cmd))
    r = subprocess.run([str(c) for c in cmd], **kw)
    if r.returncode != 0:
        fail(f"command failed ({r.returncode}): {cmd[0]}")
    return r


def version_key(name):
    return [int(p) if p.isdigit() else p for p in re.split(r"[.\-]", name)]


def find_sdk(explicit):
    for candidate in (explicit, os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT"),
                      str(Path.home() / "Android" / "Sdk"), str(Path.home() / "Library" / "Android" / "sdk"),
                      str(Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk") if os.environ.get("LOCALAPPDATA") else None):
        if candidate and (Path(candidate) / "build-tools").is_dir():
            return Path(candidate)
    fail("Android SDK not found (set ANDROID_HOME)")


def latest_dir(parent, prefix=""):
    if not parent.is_dir():
        return None
    dirs = [d for d in parent.iterdir() if d.is_dir() and d.name.startswith(prefix)]
    if not dirs:
        return None
    return max(dirs, key=lambda d: version_key(d.name[len(prefix):]))


def tool(build_tools, name):
    for suffix in ("", ".exe", ".bat"):
        p = build_tools / (name + suffix)
        if p.exists():
            return p
    fail(f"{name} not found in {build_tools}")


def find_ndk(sdk, explicit):
    for candidate in (explicit, os.environ.get("ANDROID_NDK_HOME"), os.environ.get("ANDROID_NDK_ROOT")):
        if candidate and Path(candidate, "build", "cmake", "android.toolchain.cmake").exists():
            return Path(candidate)
    ndk = latest_dir(sdk / "ndk")
    if ndk and (ndk / "build" / "cmake" / "android.toolchain.cmake").exists():
        return ndk
    fail("Android NDK not found (sdkmanager \"ndk;29.0.14206865\" or set ANDROID_NDK_HOME)")


def build_native(sdk, ndk_arg, abi, build_root, build_type):
    """libconqueror.so'yu verilen ABI için derler; yolunu döndürür."""
    ndk = find_ndk(sdk, ndk_arg)
    build_dir = build_root / f"android-{abi}"
    if not (build_dir / "CMakeCache.txt").exists():
        generator = ["-G", "Ninja"] if shutil.which("ninja") else []
        run(["cmake", "-S", ENGINE_ROOT, "-B", build_dir, *generator,
             f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build' / 'cmake' / 'android.toolchain.cmake'}",
             f"-DANDROID_ABI={abi}", f"-DANDROID_PLATFORM=android-{MIN_SDK}", "-DANDROID_STL=c++_static",
             f"-DCMAKE_BUILD_TYPE={build_type}"])
    run(["cmake", "--build", build_dir, "--target", "conqueror", "-j", str(max(2, os.cpu_count() or 2))])
    lib = build_dir / "lib" / abi / "libconqueror.so"
    if not lib.exists():
        fail(f"build finished but {lib} is missing")
    return lib


def strip_library(lib, work_dir, sdk, ndk_arg):
    """Hata ayıklama bilgisini atar (NDK llvm-strip); bulunamazsa kitaplık olduğu gibi kullanılır."""
    try:
        ndk = find_ndk(sdk, ndk_arg)
    except SystemExit:
        return lib
    strip = next(iter(sorted((ndk / "toolchains" / "llvm" / "prebuilt").glob("*/bin/llvm-strip*"))), None)
    if not strip:
        return lib
    work_dir.mkdir(parents=True, exist_ok=True)
    out = work_dir / lib.name
    r = subprocess.run([str(strip), "--strip-unneeded", "-o", str(out), str(lib)])
    if r.returncode != 0 or not out.exists():
        return lib
    log(f"stripped {lib.name}: {lib.stat().st_size // (1024 * 1024)} MB -> {out.stat().st_size // (1024 * 1024)} MB")
    return out


def find_native(abi, lib_dir, build_root):
    candidates = []
    if lib_dir:
        candidates += [Path(lib_dir) / abi / "libconqueror.so", Path(lib_dir) / "lib" / abi / "libconqueror.so",
                       Path(lib_dir) / "libconqueror.so"]
    candidates += [build_root / f"android-{abi}" / "lib" / abi / "libconqueror.so",
                   build_root / f"android-{abi}" / "lib" / "libconqueror.so"]
    for c in candidates:
        if c.exists():
            return c
    return None


def read_project(game_dir):
    """(.cqproj yolu, oyun adı, sürüm) — basit YAML okuması (bağımlılık yok)."""
    projects = sorted(game_dir.glob("*.cqproj"))
    if not projects:
        fail(f"no .cqproj in {game_dir} (build the game in the editor first, or pass the project folder)")
    text = projects[0].read_text(encoding="utf-8", errors="replace")
    name = re.search(r"^\s*Name:\s*(.+?)\s*$", text, re.M)
    version = re.search(r"^\s*Version:\s*(.+?)\s*$", text, re.M)
    clean = lambda m, d: (m.group(1).strip().strip("'\"") if m else d) or d
    return projects[0], clean(name, projects[0].stem), clean(version, "1.0")


def is_desktop_binary(path):
    if path.suffix.lower() in (".exe", ".dll", ".so", ".dylib", ".pdb"):
        return True
    if path.suffix == "" and path.is_file():
        with open(path, "rb") as f:
            return f.read(4) == b"\x7fELF"
    return False


def collect_game_files(game_dir):
    """Oyun klasöründeki paketlenecek dosyalar (masaüstü ikilileri ve lib/ hariç)."""
    files = []
    for root, dirs, names in os.walk(game_dir):
        rel_root = Path(root).relative_to(game_dir)
        if rel_root == Path("."):
            dirs[:] = [d for d in dirs if d not in ("lib", "Library", "Logs", "Temp", ".git")]
        dirs.sort()
        for n in sorted(names):
            p = Path(root) / n
            if rel_root == Path(".") and is_desktop_binary(p):
                continue
            if n in ("imgui.ini",) or n.endswith((".log", ".tmp")):
                continue
            files.append(p.relative_to(game_dir).as_posix())
    return files


def manifest_xml(package, label, version_code, version_name, orientation, automotive, debuggable, icon=False):
    automotive_feature = ('    <uses-feature android:name="android.hardware.type.automotive" android:required="true"/>\n'
                          if automotive else "")
    # Araçta sistem çubukları gizlenemez: tam ekran olmayan tema ve kenardan kenara zorlaması olmayan hedef SDK ile
    # pencere çubukların arasındaki alana yerleşir
    theme = "@android:style/Theme.DeviceDefault.NoActionBar" if automotive else "@android:style/Theme.NoTitleBar.Fullscreen"
    target_sdk = AUTOMOTIVE_TARGET_SDK if automotive else TARGET_SDK
    icon_attr = ' android:icon="@mipmap/ic_launcher"' if icon else ""
    return f"""<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="{package}" android:versionCode="{version_code}" android:versionName="{version_name}">
    <uses-sdk android:minSdkVersion="{MIN_SDK}" android:targetSdkVersion="{target_sdk}"/>
    <uses-feature android:glEsVersion="0x00030000" android:required="true"/>
    <uses-feature android:name="android.hardware.touchscreen" android:required="false"/>
    <uses-feature android:name="android.hardware.gamepad" android:required="false"/>
{automotive_feature}    <uses-permission android:name="android.permission.INTERNET"/>
    <application android:label="{label}"{icon_attr} android:hasCode="false" android:extractNativeLibs="true"
        android:isGame="true" android:appCategory="game" android:debuggable="{str(debuggable).lower()}">
        <activity android:name="android.app.NativeActivity" android:exported="true"
            android:configChanges="orientation|screenSize|screenLayout|smallestScreenSize|keyboardHidden|keyboard|navigation|uiMode|density"
            android:screenOrientation="{orientation}" android:launchMode="singleTask"
            android:theme="{theme}">
            <meta-data android:name="android.app.lib_name" android:value="conqueror"/>
            <intent-filter>
                <action android:name="android.intent.action.MAIN"/>
                <category android:name="android.intent.category.LAUNCHER"/>
            </intent-filter>
        </activity>
    </application>
</manifest>
"""


def ensure_keystore(path, keytool):
    if path.exists():
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    run([keytool, "-genkeypair", "-keystore", path, "-storepass", "android", "-keypass", "android", "-alias", "androiddebugkey",
         "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000", "-dname", "CN=Android Debug,O=Android,C=US"],
        stdout=subprocess.DEVNULL)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game_dir", help="built game folder (or project folder) containing the .cqproj")
    ap.add_argument("--out", help="output .apk (default: <game_dir>/../<Name>.apk)")
    ap.add_argument("--abi", action="append", help=f"target ABI (repeatable; default {' '.join(DEFAULT_ABIS)})")
    ap.add_argument("--lib-dir", help="folder with <abi>/libconqueror.so")
    ap.add_argument("--build-root", default=str(ENGINE_ROOT / "build"), help="where android-<abi> build folders live")
    ap.add_argument("--build", action="store_true", help="compile libconqueror.so with the NDK if missing")
    ap.add_argument("--rebuild", action="store_true", help="always (re)compile libconqueror.so")
    ap.add_argument("--build-type", default="Release")
    ap.add_argument("--package", help="application id (default com.conqueror.<name>)")
    ap.add_argument("--label", help="launcher label (default: project name)")
    ap.add_argument("--version-code", type=int, default=1)
    ap.add_argument("--orientation", default="sensorLandscape",
                    choices=["sensorLandscape", "landscape", "portrait", "sensorPortrait", "fullSensor", "unspecified"])
    ap.add_argument("--automotive", action="store_true", help="Android Automotive OS (requires android.hardware.type.automotive)")
    ap.add_argument("--icon", help="launcher icon .png (square, 192 px or larger)")
    ap.add_argument("--release", action="store_true", help="not debuggable")
    ap.add_argument("--keystore", help="signing keystore (default: ~/.android/debug.keystore, created if missing)")
    ap.add_argument("--ks-pass", default="android")
    ap.add_argument("--key-alias", default="androiddebugkey")
    ap.add_argument("--sdk", help="Android SDK folder")
    ap.add_argument("--ndk", help="Android NDK folder")
    ap.add_argument("--install", action="store_true", help="adb install -r the APK")
    ap.add_argument("--run", action="store_true", help="start the game after installing")
    args = ap.parse_args()

    game_dir = Path(args.game_dir).resolve()
    if not game_dir.is_dir():
        fail(f"{game_dir} is not a folder")
    project_file, name, version_name = read_project(game_dir)
    safe = re.sub(r"[^A-Za-z0-9_]", "", name.replace(" ", "_")) or "game"
    if safe[0].isdigit():
        safe = "g" + safe
    package = args.package or f"com.conqueror.{safe.lower()}"
    label = (args.label or name).replace("&", "&amp;").replace("\"", "&quot;").replace("<", "&lt;")
    out = Path(args.out).resolve() if args.out else game_dir.parent / f"{safe}.apk"
    abis = args.abi or DEFAULT_ABIS
    build_root = Path(args.build_root).resolve()

    sdk = find_sdk(args.sdk)
    build_tools = latest_dir(sdk / "build-tools")
    platform = latest_dir(sdk / "platforms", "android-")
    if not build_tools or not platform or not (platform / "android.jar").exists():
        fail(f"build-tools / platforms missing in {sdk} (sdkmanager \"build-tools;37.0.0\" \"platforms;android-{TARGET_SDK}\")")
    aapt2, zipalign, apksigner = tool(build_tools, "aapt2"), tool(build_tools, "zipalign"), tool(build_tools, "apksigner")
    log(f"SDK {sdk}  build-tools {build_tools.name}  {platform.name}  package {package}")

    # 1) Oyuncu kitaplıkları
    libs = {}
    for abi in abis:
        lib = None if args.rebuild else find_native(abi, args.lib_dir, build_root)
        if lib is None:
            if not (args.build or args.rebuild):
                fail(f"libconqueror.so for {abi} not found (pass --build to compile it with the NDK)")
            lib = build_native(sdk, args.ndk, abi, build_root, args.build_type)
        libs[abi] = lib
        log(f"{abi}: {lib} ({lib.stat().st_size // 1024} KB)")

    # 2) Oyun dosyaları + sürüm damgası (değişince cihazda yeniden çıkarılır)
    files = collect_game_files(game_dir)
    digest = hashlib.sha1()
    for rel in files:
        st = (game_dir / rel).stat()
        digest.update(f"{rel}\0{st.st_size}\0{int(st.st_mtime)}\n".encode())
    for abi, lib in libs.items():
        digest.update(f"{abi}\0{lib.stat().st_size}\0{int(lib.stat().st_mtime)}\n".encode())
    stamp = f"{version_name}-{digest.hexdigest()[:16]}"
    log(f"{len(files)} game files, asset version {stamp}")

    with tempfile.TemporaryDirectory(prefix="cq_apk_") as tmp:
        tmp = Path(tmp)
        # Başlatıcı simgesi: res/mipmap-xxxhdpi/ic_launcher.png (Android diğer yoğunluklar için ölçekler)
        icon = Path(args.icon) if args.icon else None
        res_flags = []
        if icon and icon.is_file():
            mip = tmp / "res" / "mipmap-xxxhdpi"
            mip.mkdir(parents=True)
            shutil.copyfile(icon, mip / "ic_launcher.png")
            run([aapt2, "compile", "--dir", tmp / "res", "-o", tmp / "res.zip"])
            res_flags = ["-R", tmp / "res.zip", "--auto-add-overlay"]
        elif args.icon:
            log(f"icon not found: {args.icon} (APK without a launcher icon)")
        (tmp / "AndroidManifest.xml").write_text(manifest_xml(package, label, args.version_code, version_name,
                                                              args.orientation, args.automotive, not args.release, bool(res_flags)),
                                                 encoding="utf-8")
        base = tmp / "base.apk"
        run([aapt2, "link", "-o", base, "-I", platform / "android.jar", "--manifest", tmp / "AndroidManifest.xml",
             "--min-sdk-version", str(MIN_SDK),
             "--target-sdk-version", str(AUTOMOTIVE_TARGET_SDK if args.automotive else TARGET_SDK)] + res_flags)

        # aapt2'nin varlık süzgeci ("_" ile başlayan klasörler, "." dosyaları) oyun dosyalarını atmasın: zipfile ile ekle
        unaligned = tmp / "unaligned.apk"
        shutil.copyfile(base, unaligned)
        stored = (".png", ".jpg", ".jpeg", ".ogg", ".mp3", ".zst", ".ktx2", ".webp")
        with zipfile.ZipFile(unaligned, "a") as z:
            z.writestr("assets/cq_files.txt", stamp + "\n" + "\n".join(files) + "\n", zipfile.ZIP_DEFLATED)
            for rel in files:
                compress = zipfile.ZIP_STORED if rel.lower().endswith(stored) else zipfile.ZIP_DEFLATED
                z.write(game_dir / rel, "assets/" + rel, compress)
            for abi, lib in libs.items():
                z.write(strip_library(lib, tmp / abi, sdk, args.ndk), f"lib/{abi}/libconqueror.so", zipfile.ZIP_DEFLATED)

        aligned = tmp / "aligned.apk"
        # -P 16: 16 KB sayfa boyutlu cihazlar için .so hizası (build-tools 35+); eski sürümlerde -p
        r = subprocess.run([str(zipalign), "-P", "16", "-f", "4", str(unaligned), str(aligned)], capture_output=True, text=True)
        if r.returncode != 0:
            run([zipalign, "-p", "-f", "4", unaligned, aligned])

        keystore = Path(args.keystore) if args.keystore else Path.home() / ".android" / "debug.keystore"
        if not args.keystore:
            keytool = shutil.which("keytool") or fail("keytool not found (install a JDK)")
            ensure_keystore(keystore, keytool)
        out.parent.mkdir(parents=True, exist_ok=True)
        run([apksigner, "sign", "--ks", keystore, "--ks-pass", f"pass:{args.ks_pass}", "--ks-key-alias", args.key_alias,
             "--out", out, aligned])
    log(f"APK ready: {out} ({out.stat().st_size // (1024 * 1024)} MB)")

    if args.install or args.run:
        adb = sdk / "platform-tools" / ("adb.exe" if os.name == "nt" else "adb")
        run([adb, "install", "-r", out])
        if args.run:
            run([adb, "shell", "am", "start", "-n", f"{package}/android.app.NativeActivity"])


if __name__ == "__main__":
    main()
