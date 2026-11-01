#!/usr/bin/env python3
"""windres sarmalayıcısı: windres ön işlemciyi kabuk komutuyla çağırır, yolda kesme işareti varsa
(ör. "Conqueror's_Engine") bozulur. .rc dosyası düz adlı geçici bir klasöre kopyalanır; asıl klasörü ve diğer -I
klasörleri düz adlı sembolik bağlar üzerinden verilir ("../zlib.h" gibi göreli eklemeler de çalışır)."""
import os
import shutil
import subprocess
import sys
import tempfile

real = os.environ.get("CQ_WINDRES", "x86_64-w64-mingw32-windres")
args = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix="cqrc_") as tmp:
    links = {}

    def plain(path):
        path = os.path.abspath(path)
        if path not in links:
            links[path] = os.path.join(tmp, "inc%d" % len(links))
            os.symlink(path, links[path])
        return links[path]

    out = []
    i = 0
    while i < len(args):
        a = args[i]
        if a.endswith(".rc") and os.path.isfile(a):
            src = plain(os.path.dirname(os.path.abspath(a)))
            copy = os.path.join(tmp, "input.rc")
            shutil.copyfile(a, copy)
            out += ["-I", src, copy]
        elif a in ("-I", "--include-dir") and i + 1 < len(args):
            out += [a, plain(args[i + 1])]
            i += 1
        elif a.startswith("-I") and len(a) > 2:
            out.append("-I" + plain(a[2:]))
        else:
            out.append(a)
        i += 1
    sys.exit(subprocess.call([real] + out))
