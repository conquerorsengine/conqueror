#!/bin/sh
# ARM64 Linux (aarch64: Raspberry Pi 4 / 5 with Raspberry Pi OS 64-bit, ARM laptops and servers, NVIDIA Jetson ...) sistem kökü:
# Debian 13 (trixie) arm64 paketlerinden, x86_64 Linux'ta çapraz derleme için. Raspberry Pi OS bookworm'un glibc'si (2.36) bunun
# 2.41'inden eski olduğundan Pi için CQ_ARM64_SUITE=bookworm verin (gcc 12).
#   Tools/Linux/make-arm64-sysroot.sh <hedef klasör>
# Gerekenler: curl, xz, ar (binutils), tar, python3
set -eu
DEST="${1:?usage: make-arm64-sysroot.sh <sysroot dir>}"
SUITE="${CQ_ARM64_SUITE:-trixie}"
case "$SUITE" in
    bookworm) GCCV=12 ;;
    *) GCCV=14 ;;
esac
WORK="$(mktemp -d)"
trap 'rm -r -- "$WORK"' EXIT
MIRROR="https://deb.debian.org/debian"
mkdir -p "$DEST"
curl -sfL -o "$WORK/Packages.xz" "$MIRROR/dists/$SUITE/main/binary-arm64/Packages.xz"
xz -d "$WORK/Packages.xz"
python3 - "$WORK/Packages" "$WORK/files.txt" "$GCCV" <<'PY'
import re, sys
pk = {}
cur = {}
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    line = line.rstrip("\n")
    if not line:
        if "Package" in cur:
            pk.setdefault(cur["Package"], []).append(cur)
        cur = {}
        continue
    m = re.match(r"^([A-Za-z0-9.+-]+): (.*)$", line)
    if m:
        cur[m.group(1)] = m.group(2)
g = sys.argv[3]
want = ["libc6", "libc6-dev", "linux-libc-dev", "libstdc++6", "libgcc-s1", f"libstdc++-{g}-dev", f"libgcc-{g}-dev",
        "libx11-dev", "libx11-6", "x11proto-dev", "libxrandr-dev", "libxrandr2", "libxinerama-dev", "libxinerama1",
        "libxcursor-dev", "libxcursor1", "libxi-dev", "libxi6", "libxext-dev", "libxext6", "libxrender-dev", "libxrender1",
        "libxfixes-dev", "libxfixes3", "libxcb1-dev", "libxcb1", "libxau-dev", "libxau6", "libxdmcp-dev", "libxdmcp6",
        "libbsd0", "libmd0"]
with open(sys.argv[2], "w") as out:
    for w in want:
        if w not in pk:
            sys.exit("missing package " + w)
        out.write(pk[w][-1]["Filename"] + "\n")
PY
mkdir -p "$WORK/deb"
while read -r f; do
    n="$(basename "$f")"
    curl -sfL -o "$WORK/$n" "$MIRROR/$f"
    (cd "$WORK/deb" && rm -f data.tar.* control.tar.* debian-binary && ar x "$WORK/$n" && tar -xf data.tar.* -C "$DEST")
done < "$WORK/files.txt"
# Debian birleşik /usr düzeni: libc.so betiği /lib/ld-linux-aarch64.so.1 arar
[ -e "$DEST/lib" ] || ln -s usr/lib "$DEST/lib"
echo "ARM64 Linux sysroot ($SUITE) ready: $DEST"
