#!/bin/sh
# Linux oyuncusunu x86_64 Linux'tan başka işlemcilere çapraz derlemek için sistem kökü (Debian paketlerinden):
#   arm64    aarch64: Raspberry Pi 4 / 5 (64-bit), ARM dizüstüleri ve sunucuları, NVIDIA Jetson
#   armhf    ARMv7 32-bit: Raspberry Pi OS 32-bit, eski tek kart bilgisayarlar
#   i386     x86 32-bit: eski PC'ler
#   riscv64  RISC-V 64: StarFive VisionFive 2, Milk-V, SiFive
#   ppc64el  POWER8+ little endian: IBM POWER, Raptor Talos
#   loong64  LoongArch 64: Loongson 3A5000 / 3A6000 (Debian forky)
#   Tools/Linux/make-linux-sysroot.sh <arch> <hedef klasör>
# Raspberry Pi OS bookworm'un glibc'si (2.36) trixie'ninkinden (2.41) eski olduğundan Pi için CQ_LINUX_SUITE=bookworm verin.
# Gerekenler: curl, xz, ar (binutils), tar, python3
set -eu
ARCH="${1:?usage: make-linux-sysroot.sh <arm64|armhf|i386|riscv64|ppc64el|loong64> <sysroot dir>}"
DEST="${2:?usage: make-linux-sysroot.sh <arch> <sysroot dir>}"
case "$ARCH" in
    arm64|armhf|i386|riscv64|ppc64el) DEFAULT_SUITE=trixie ;;
    loong64) DEFAULT_SUITE=forky ;;
    *) echo "unknown architecture: $ARCH" >&2; exit 2 ;;
esac
SUITE="${CQ_LINUX_SUITE:-${CQ_ARM64_SUITE:-$DEFAULT_SUITE}}"
WORK="$(mktemp -d)"
trap 'rm -r -- "$WORK"' EXIT
MIRROR="https://deb.debian.org/debian"
mkdir -p "$DEST"
curl -sfL -o "$WORK/Packages.xz" "$MIRROR/dists/$SUITE/main/binary-$ARCH/Packages.xz"
xz -d "$WORK/Packages.xz"
python3 - "$WORK/Packages" "$WORK/files.txt" <<'PY'
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
# GCC'nin C++ kitaplığı: libstdc++-N-dev'in en yeni sürümü (N dağıtıma göre değişir)
def newest(prefix, suffix):
    best = None
    for name in pk:
        m = re.fullmatch(re.escape(prefix) + r"(\d+)" + re.escape(suffix), name)
        if m and (best is None or int(m.group(1)) > best):
            best = int(m.group(1))
    if best is None:
        sys.exit("missing package " + prefix + "N" + suffix)
    return f"{prefix}{best}{suffix}"
want = ["libc6", "libc6-dev", "linux-libc-dev", "libstdc++6", "libgcc-s1", newest("libstdc++-", "-dev"), newest("libgcc-", "-dev"),
        "libx11-dev", "libx11-6", "x11proto-dev", "libxrandr-dev", "libxrandr2", "libxinerama-dev", "libxinerama1",
        "libxcursor-dev", "libxcursor1", "libxi-dev", "libxi6", "libxext-dev", "libxext6", "libxrender-dev", "libxrender1",
        "libxfixes-dev", "libxfixes3", "libxcb1-dev", "libxcb1", "libxau-dev", "libxau6", "libxdmcp-dev", "libxdmcp6",
        "libbsd0", "libmd0", "libatomic1"]
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
# Debian birleşik /usr düzeni: libc.so betiği /lib/ld-linux-*.so arar
[ -e "$DEST/lib" ] || ln -s usr/lib "$DEST/lib"
[ -e "$DEST/lib64" ] || ln -s usr/lib64 "$DEST/lib64" 2>/dev/null || true
echo "Linux $ARCH sysroot ($SUITE) ready: $DEST"
