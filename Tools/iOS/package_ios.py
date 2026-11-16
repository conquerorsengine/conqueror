#!/usr/bin/env python3
"""Conqueror Engine -> iOS / iPadOS uygulaması (.app, aygıt için ayrıca .ipa).

Derlenmiş bir oyun klasörünü (editörde Build ile üretilen klasör: .cqproj, Assets/, Resources/, Engine/src/Shaders ...) UIKit tabanlı
bir iOS uygulamasına dönüştürür. Oyuncu (conqueror.app) motor kaynaklarından CMake ile iOS için derlenir (Metal; Xcode gerekir,
yalnız macOS'ta). Oyun dosyaları uygulamanın içinde game/ klasörüne girer, ilk açılışta yazılabilir klasöre kopyalanır.

  python3 Tools/iOS/package_ios.py <oyun-klasörü> --out MyGame.app [--simulator] [--team TEAMID] [--bundle-id com.company.game]
                                   [--name "My Game"] [--version 1.0] [--build-number 1] [--orientation landscape|portrait|all]
                                   [--devices universal|iphone|ipad] [--icon icon.png] [--install] [--run] [--screenshot shot.png]

Takım kimliği (Team ID) yoksa uygulama iOS Simülatörü için derlenir (imzasız, ad-hoc). Takım kimliğiyle aygıt için derlenir ve
anahtar zincirindeki "Apple Development" kimliği + o takımın hazırlık profiliyle (provisioning profile) imzalanır; .ipa da yazılır.
"""

import argparse
import hashlib
import os
import platform
import plistlib
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[2]
MIN_IOS = "16.0"


def log(msg):
    print(f"[package_ios] {msg}", flush=True)


def fail(msg):
    print(f"[package_ios] ERROR: {msg}", file=sys.stderr, flush=True)
    sys.exit(1)


def run(cmd, check=True, capture=False, **kw):
    log("$ " + " ".join(str(c) for c in cmd))
    r = subprocess.run([str(c) for c in cmd], text=True, capture_output=capture, **kw)
    if check and r.returncode != 0:
        if capture:
            sys.stderr.write((r.stdout or "") + (r.stderr or ""))
        fail(f"command failed ({r.returncode}): {cmd[0]} {cmd[1] if len(cmd) > 1 else ''}")
    return r


def build_player(build_root: Path, simulator: bool, jobs: int) -> Path:
    """Oyuncuyu iOS için derler (ilk seferde bütün motor, sonra yalnız değişenler); conqueror.app yolunu döner."""
    sdk = "iphonesimulator" if simulator else "iphoneos"
    arch = "arm64" if not simulator else ("arm64" if platform.machine() in ("arm64", "aarch64") else "x86_64")
    build = build_root / f"ios-{sdk}-{arch}"
    if not (build / "build.ninja").exists():
        generator = ["-G", "Ninja"] if shutil.which("ninja") else []
        run(["cmake", "-S", ENGINE_ROOT, "-B", build, *generator, "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_SYSTEM_NAME=iOS",
             f"-DCMAKE_OSX_SYSROOT={sdk}", f"-DCMAKE_OSX_ARCHITECTURES={arch}", f"-DCMAKE_OSX_DEPLOYMENT_TARGET={MIN_IOS}",
             "-DCQ_RUNTIME_ONLY=ON"])
    run(["cmake", "--build", build, "--target", "ConquerorRuntime", "--parallel", str(jobs)])
    for candidate in (build / "bin" / "conqueror.app", build / "Runtime" / "conqueror.app"):
        if (candidate / "conqueror").exists():
            return candidate
    found = list(build.rglob("conqueror.app"))
    if not found:
        fail(f"conqueror.app was not produced in {build}")
    return found[0]


def game_version(game: Path) -> str:
    """Oyun dosyalarının damgası: değişince uygulama ilk açılışta dosyaları yeniden kopyalar."""
    h = hashlib.sha256()
    for f in sorted(game.rglob("*")):
        if f.is_file():
            st = f.stat()
            h.update(f"{f.relative_to(game)}|{st.st_size}|{int(st.st_mtime)}".encode())
    return h.hexdigest()[:16] + "-" + str(int(time.time()))


def write_info_plist(app: Path, args, simulator: bool):
    plist_path = app / "Info.plist"
    with open(plist_path, "rb") as f:
        info = plistlib.load(f)
    info["CFBundleIdentifier"] = args.bundle_id
    info["CFBundleName"] = args.name[:15]
    info["CFBundleDisplayName"] = args.name
    info["CFBundleShortVersionString"] = args.version
    info["CFBundleVersion"] = str(args.build_number)
    info["MinimumOSVersion"] = args.min_ios
    info["CFBundleSupportedPlatforms"] = ["iPhoneSimulator" if simulator else "iPhoneOS"]
    families = {"iphone": [1], "ipad": [2], "universal": [1, 2]}[args.devices]
    info["UIDeviceFamily"] = families
    orientations = {
        "landscape": ["UIInterfaceOrientationLandscapeLeft", "UIInterfaceOrientationLandscapeRight"],
        "portrait": ["UIInterfaceOrientationPortrait", "UIInterfaceOrientationPortraitUpsideDown"],
        "all": ["UIInterfaceOrientationPortrait", "UIInterfaceOrientationLandscapeLeft", "UIInterfaceOrientationLandscapeRight",
                "UIInterfaceOrientationPortraitUpsideDown"],
    }[args.orientation]
    info["UISupportedInterfaceOrientations"] = orientations
    info["UISupportedInterfaceOrientations~ipad"] = orientations
    if args.camera_usage:
        info["NSCameraUsageDescription"] = args.camera_usage
    if getattr(args, "player", False):
        # Conqueror Player (264): .cqspace oyunlarını Dosyalar / AirDrop / "Open in" ile açar
        info["CFBundleDocumentTypes"] = [{"CFBundleTypeName": "Conqueror game", "CFBundleTypeRole": "Viewer",
                                          "LSHandlerRank": "Owner", "LSItemContentTypes": ["com.conqueror.cqspace"]}]
        info["UTExportedTypeDeclarations"] = [{"UTTypeIdentifier": "com.conqueror.cqspace", "UTTypeDescription": "Conqueror game",
                                               "UTTypeConformsTo": ["public.data"],
                                               "UTTypeTagSpecification": {"public.filename-extension": ["cqspace"],
                                                                          "public.mime-type": ["application/x-cqspace"]}}]
        info["LSSupportsOpeningDocumentsInPlace"] = False
    if args.icon:
        info["CFBundleIcons"] = {"CFBundlePrimaryIcon": {"CFBundleIconFiles": ["AppIcon60x60", "AppIcon76x76", "AppIcon83.5x83.5"]}}
        info["CFBundleIcons~ipad"] = info["CFBundleIcons"]
    with open(plist_path, "wb") as f:
        plistlib.dump(info, f)


def add_icon(app: Path, icon: Path):
    """Uygulama simgesi: sips ile iPhone / iPad boyutları (asset catalog gerekmeden CFBundleIconFiles)."""
    sizes = {"AppIcon60x60@2x.png": 120, "AppIcon60x60@3x.png": 180, "AppIcon76x76@2x~ipad.png": 152, "AppIcon83.5x83.5@2x~ipad.png": 167}
    for name, px in sizes.items():
        run(["sips", "-z", str(px), str(px), icon, "--out", app / name], capture=True)


def find_identity(team: str) -> str:
    out = run(["security", "find-identity", "-v", "-p", "codesigning"], capture=True).stdout
    for line in out.splitlines():
        if f"({team})" in line or team in line:
            if "Apple Development" in line or "iPhone Developer" in line or "Apple Distribution" in line or "iPhone Distribution" in line:
                return line.split('"')[1]
    fail(f"No code signing identity for team {team} in the keychain (Xcode > Settings > Accounts > Manage Certificates)")


def find_profile(team: str, bundle_id: str) -> Path:
    """Takımın bu paket kimliğine (ya da joker kimliğe) uyan hazırlık profili (Xcode'un indirdiği profiller)."""
    dirs = [Path.home() / "Library" / "MobileDevice" / "Provisioning Profiles",
            Path.home() / "Library" / "Developer" / "Xcode" / "UserData" / "Provisioning Profiles"]
    best = None
    for d in dirs:
        for p in sorted(d.glob("*.mobileprovision")) if d.exists() else []:
            r = subprocess.run(["security", "cms", "-D", "-i", str(p)], capture_output=True)
            if r.returncode != 0:
                continue
            data = plistlib.loads(r.stdout)
            app_id = data.get("Entitlements", {}).get("application-identifier", "")
            if team not in data.get("TeamIdentifier", []):
                continue
            if data.get("ExpirationDate") and data["ExpirationDate"].timestamp() < time.time():
                continue
            suffix = app_id.split(".", 1)[1] if "." in app_id else ""
            if suffix == bundle_id:
                return p
            if suffix.endswith("*") and bundle_id.startswith(suffix[:-1]) and best is None:
                best = p
    if best:
        return best
    fail(f"No provisioning profile for {bundle_id} (team {team}). Create one in Xcode (open any project with this bundle identifier "
         "and enable automatic signing) or in the Apple Developer portal.")


def sign(app: Path, simulator: bool, team: str):
    if simulator:
        run(["codesign", "--force", "--sign", "-", "--timestamp=none", app], capture=True)
        return
    identity = find_identity(team)
    profile = find_profile(team, plistlib.load(open(app / "Info.plist", "rb"))["CFBundleIdentifier"])
    shutil.copy2(profile, app / "embedded.mobileprovision")
    data = plistlib.loads(subprocess.run(["security", "cms", "-D", "-i", str(profile)], capture_output=True, check=True).stdout)
    with tempfile.NamedTemporaryFile("wb", suffix=".plist", delete=False) as ent:
        plistlib.dump(data.get("Entitlements", {}), ent)
    run(["codesign", "--force", "--sign", identity, "--entitlements", ent.name, "--timestamp=none", app], capture=True)
    os.unlink(ent.name)


def make_ipa(app: Path) -> Path:
    ipa = app.with_suffix(".ipa")
    with tempfile.TemporaryDirectory() as tmp:
        payload = Path(tmp) / "Payload"
        payload.mkdir()
        shutil.copytree(app, payload / app.name, symlinks=True)
        if ipa.exists():
            ipa.unlink()
        run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", payload, ipa])
    return ipa


def simulator_device() -> str:
    """Açık bir simülatör ya da ilk iPhone simülatörü (açılır)."""
    import json
    data = json.loads(run(["xcrun", "simctl", "list", "devices", "available", "-j"], capture=True).stdout)
    devices = [d for runtime, ds in data.get("devices", {}).items() if "iOS" in runtime for d in ds]
    for d in devices:
        if d.get("state") == "Booted":
            return d["udid"]
    phones = [d for d in devices if "iPhone" in d.get("name", "")] or devices
    if not phones:
        fail("No iOS simulator is installed (Xcode > Settings > Components)")
    udid = phones[-1]["udid"]
    run(["xcrun", "simctl", "boot", udid], check=False)
    run(["xcrun", "simctl", "bootstatus", udid, "-b"], check=False)
    return udid


def main():
    ap = argparse.ArgumentParser(description="Package a built Conqueror game as an iOS / iPadOS app")
    ap.add_argument("game", type=Path, help="built game folder (Build output of the editor)")
    ap.add_argument("--out", type=Path, required=True, help="output .app path")
    ap.add_argument("--simulator", action="store_true", help="build for the iOS Simulator (default when no --team is given)")
    ap.add_argument("--team", default="", help="Apple Developer Team ID: build for devices and sign")
    ap.add_argument("--bundle-id", default="com.conqueror.game")
    ap.add_argument("--name", default="")
    ap.add_argument("--version", default="1.0")
    ap.add_argument("--build-number", default="1")
    ap.add_argument("--min-ios", default=MIN_IOS)
    ap.add_argument("--orientation", choices=["landscape", "portrait", "all"], default="landscape")
    ap.add_argument("--devices", choices=["universal", "iphone", "ipad"], default="universal")
    ap.add_argument("--camera-usage", default="", help="NSCameraUsageDescription (AR)")
    ap.add_argument("--player", action="store_true", help="Conqueror Player app: opens .cqspace games (give an empty game folder)")
    ap.add_argument("--icon", type=Path)
    ap.add_argument("--build-root", type=Path, default=ENGINE_ROOT / "build-ios")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--install", action="store_true", help="install on the simulator / connected device")
    ap.add_argument("--run", action="store_true", help="start the app after installing")
    ap.add_argument("--screenshot", type=Path, help="simulator: screenshot after --wait seconds (tests)")
    ap.add_argument("--wait", type=float, default=20.0)
    args = ap.parse_args()

    if sys.platform != "darwin":
        fail("iOS apps are built on macOS with Xcode")
    if not shutil.which("xcrun"):
        fail("Xcode command line tools not found (xcode-select --install)")
    game = args.game.resolve()
    if not game.is_dir() or not any(game.glob("*.cqproj")):
        fail(f"{game} is not a built game folder (no .cqproj)")
    simulator = args.simulator or not args.team
    if not args.name:
        args.name = next(game.glob("*.cqproj")).stem
    if not all(c.isalnum() or c in ".-" for c in args.bundle_id) or "." not in args.bundle_id:
        fail(f'Bundle identifier "{args.bundle_id}" is not valid (letters, digits, - and at least one dot, e.g. com.company.game)')
    log(f"Target: {'iOS Simulator' if simulator else 'iOS devices (team ' + args.team + ')'}")

    player = build_player(args.build_root.resolve(), simulator, args.jobs)
    out = args.out.resolve()
    if out.exists():
        shutil.rmtree(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(player, out, symlinks=True)
    # Oyun dosyaları: masaüstü çalıştırılabilirleri / kitaplıkları alınmaz
    skip = {"lib", "Cache", "Logs"}
    shutil.copytree(game, out / "game", ignore=lambda d, names: [n for n in names if Path(d) == game and n in skip])
    (out / "game" / ".cq_game_version").write_text(game_version(game) + "\n")
    write_info_plist(out, args, simulator)
    if args.icon and args.icon.exists():
        add_icon(out, args.icon)
    sign(out, simulator, args.team)
    log(f"App: {out}")
    if not simulator:
        log(f"IPA: {make_ipa(out)}")

    if args.install or args.run or args.screenshot:
        if simulator:
            udid = simulator_device()
            run(["xcrun", "simctl", "install", udid, out])
            if args.run or args.screenshot:
                run(["xcrun", "simctl", "launch", udid, args.bundle_id])
            if args.screenshot:
                time.sleep(args.wait)
                run(["xcrun", "simctl", "io", udid, "screenshot", args.screenshot])
                log(f"Screenshot: {args.screenshot}")
        else:
            run(["xcrun", "devicectl", "device", "install", "app", "--device", "any", out], check=False)
            if args.run:
                run(["xcrun", "devicectl", "device", "process", "launch", "--device", "any", args.bundle_id], check=False)


if __name__ == "__main__":
    main()
