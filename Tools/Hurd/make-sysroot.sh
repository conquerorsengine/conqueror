#!/bin/sh
# GNU/Hurd (hurd-amd64) sistem kökü: Debian debian-ports paketlerinden, Linux'ta çapraz derleme için.
#   Tools/Hurd/make-sysroot.sh <hedef klasör>
# Gerekenler: curl, xz, ar (binutils), tar, python3
set -eu
DEST="${1:?usage: make-sysroot.sh <sysroot dir>}"
WORK="$(mktemp -d)"
trap 'rm -r -- "$WORK"' EXIT
MIRROR="https://deb.debian.org/debian-ports"
mkdir -p "$DEST"
curl -sfL -o "$WORK/Packages.xz" "$MIRROR/dists/unstable/main/binary-hurd-amd64/Packages.xz"
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
want = ["libc0.3", "libc0.3-dev", "hurd-dev", "gnumach-dev", "libx11-dev", "libx11-6", "x11proto-dev", "libxrandr-dev", "libxrandr2",
        "libxinerama-dev", "libxinerama1", "libxcursor-dev", "libxcursor1", "libxi-dev", "libxi6", "libxext-dev", "libxext6",
        "libxrender-dev", "libxrender1", "libxfixes-dev", "libxfixes3", "libxcb1-dev", "libxcb1", "libxau-dev", "libxau6",
        "libxdmcp-dev", "libxdmcp6", "libbsd0", "libmd0", "libstdc++6", "libgcc-s1", "libstdc++-15-dev", "libgcc-15-dev"]
with open(sys.argv[2], "w") as out:
    for w in want:
        if w not in pk:
            sys.exit("missing package " + w)
        out.write(pk[w][-1]["Filename"] + "\n")
PY
mkdir -p "$WORK/deb"
while read -r f; do
    n="$(basename "$f")"
    curl -sfL -o "$WORK/$n" "$MIRROR/$f" || curl -sfL -o "$WORK/$n" "https://deb.debian.org/debian/$f"
    (cd "$WORK/deb" && rm -f data.tar.* control.tar.* debian-binary && ar x "$WORK/$n" && tar -xf data.tar.* -C "$DEST")
done < "$WORK/files.txt"
# clang'ın Hurd hedefi __float128 desteklemiyor: glibc başlığı onu yalnız GCC için açsın
F="$DEST/usr/include/x86_64-gnu/bits/floatn.h"
python3 - "$F" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = "# define __HAVE_FLOAT128 1\n#else\n# define __HAVE_FLOAT128 0\n#endif"
if old in s and "__clang__ && defined __GNU__" not in s:
    s = s.replace(old, "# if defined __clang__ && defined __GNU__\n#  define __HAVE_FLOAT128 0\n# else\n#  define __HAVE_FLOAT128 1\n# endif\n#else\n# define __HAVE_FLOAT128 0\n#endif", 1)
    open(p, "w").write(s)
PY
# Debian birleşik /usr düzeni: libc.so betiği /lib/ld-x86-64.so.1 arar
[ -e "$DEST/lib" ] || ln -s usr/lib "$DEST/lib"
echo "GNU/Hurd sysroot ready: $DEST"
