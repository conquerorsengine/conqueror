#!/usr/bin/env python3
"""iOS Simülatöründe oyun testi (CI): uygulamayı kurar, başlatır, bekler, ekran görüntüsü alır ve sahnenin çizildiğini denetler
(siyah / tek renk değil, uygulama hâlâ çalışıyor). Başarısızlıkta uygulamanın son günlük satırlarını yazar.

  python3 Tools/iOS/simulator_test.py <MyGame.app> --screenshot shot.png [--wait 40] [--expect-log REGEX]

--expect-log: the game's own log (stdout / stderr of the app) must contain a line matching REGEX (e.g. the ARKit state).
"""

import argparse
import json
import plistlib
import re
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path


def log(msg):
    print(f"[simulator_test] {msg}", flush=True)


def sh(cmd, check=True):
    log("$ " + " ".join(str(c) for c in cmd))
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    if check and r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit(f"command failed: {cmd[:3]}")
    return r


def png_stats(path: Path):
    """8 bit RGB / RGBA PNG: ortalama ve standart sapma (parlaklık), her 4. satır / sütun."""
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, width, height, bpp = 8, b"", 0, 0, 4
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and color in (2, 6), f"unsupported PNG (depth {depth}, color {color})"
            bpp = 3 if color == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * bpp
    prev = bytearray(stride)
    total = total2 = count = 0
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        if y % 4 == 0:
            for x in range(0, width, 4):
                o = x * bpp
                lum = 0.2126 * line[o] + 0.7152 * line[o + 1] + 0.0722 * line[o + 2]
                total += lum
                total2 += lum * lum
                count += 1
        prev = line
    mean = total / max(count, 1)
    return width, height, mean, max(0.0, total2 / max(count, 1) - mean * mean) ** 0.5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("app", type=Path)
    ap.add_argument("--screenshot", type=Path, required=True)
    ap.add_argument("--wait", type=float, default=40.0)
    ap.add_argument("--expect-log", default=None)
    args = ap.parse_args()

    bundle_id = plistlib.load(open(args.app / "Info.plist", "rb"))["CFBundleIdentifier"]
    devices = json.loads(sh(["xcrun", "simctl", "list", "devices", "available", "-j"]).stdout)["devices"]
    phones = [d for runtime, ds in devices.items() if "iOS" in runtime for d in ds if "iPhone" in d.get("name", "")]
    if not phones:
        sys.exit("no iPhone simulator available")
    device = phones[-1]
    udid = device["udid"]
    log(f"Simulator: {device['name']} ({udid})")
    sh(["xcrun", "simctl", "boot", udid], check=False)
    sh(["xcrun", "simctl", "bootstatus", udid, "-b"])
    sh(["xcrun", "simctl", "install", udid, args.app])
    out_log = Path(args.screenshot).with_suffix(".log")
    launch = ["xcrun", "simctl", "launch"]
    if args.expect_log:
        launch += [f"--stdout={out_log}", f"--stderr={out_log}"]
    launched = sh(launch + [udid, bundle_id])
    pid = launched.stdout.strip().split(":")[-1].strip()
    log(f"Started {bundle_id} (pid {pid}), waiting {args.wait:.0f} s")
    time.sleep(args.wait)
    sh(["xcrun", "simctl", "io", udid, "screenshot", args.screenshot])
    running = sh(["xcrun", "simctl", "spawn", udid, "launchctl", "list"], check=False).stdout
    alive = bundle_id in running
    width, height, mean, std = png_stats(args.screenshot)
    log(f"Screenshot {width}x{height}: mean {mean:.1f}, std {std:.1f}, app running: {alive}")
    ok = alive and mean > 8.0 and std > 3.0
    if args.expect_log:
        text = out_log.read_text(errors="replace") if out_log.exists() else ""
        hits = [l for l in text.splitlines() if re.search(args.expect_log, l)]
        log(f"Game log: {len(text.splitlines())} lines, {len(hits)} matching {args.expect_log!r}")
        for l in hits[:5]:
            log("  " + l.strip())
        if not hits:
            print("\n".join(text.splitlines()[-80:]))
            ok = False
    if not ok:
        logs = sh(["xcrun", "simctl", "spawn", udid, "log", "show", "--last", "3m", "--style", "compact", "--predicate",
                   'process == "conqueror"'], check=False).stdout
        print("\n".join(logs.splitlines()[-120:]))
        crash_dir = Path.home() / "Library" / "Logs" / "DiagnosticReports"
        for report in sorted(crash_dir.glob("conqueror*"))[-1:]:
            print(report.read_text(errors="replace")[:6000])
        sys.exit("the game did not render on the iOS Simulator" if alive else "the game is not running on the iOS Simulator")
    log("OK: the game runs and renders on the iOS Simulator")


if __name__ == "__main__":
    main()
