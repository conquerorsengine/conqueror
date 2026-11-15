#!/usr/bin/env python3
"""Conqueror web oyununu yerelde sunar (Unity "Build And Run" gibi): doğru MIME türleri, .gz / .br dosyaları için
Content-Encoding ve çok iş parçacıklı oyuncunun istediği COOP / COEP başlıkları.

  python3 Tools/Web/serve.py <web-klasörü> [--port 8080] [--open]
"""

import argparse
import functools
import http.server
import mimetypes
import sys
import webbrowser
from pathlib import Path


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      ".wasm": "application/wasm", ".js": "application/javascript", ".data": "application/octet-stream",
                      ".json": "application/json"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def guess_type(self, path):
        p = str(path)
        for enc in (".br", ".gz"):
            if p.endswith(enc):
                return self.extensions_map.get(Path(p[: -len(enc)]).suffix, "application/octet-stream")
        return super().guess_type(path)

    def send_head(self):
        path = self.translate_path(self.path)
        if path.endswith(".br") and Path(path).is_file():
            # brotli: tarayıcı açar (gzip dosyaları yükleyicide açılır, başlıksız gönderilir)
            f = open(path, "rb")
            fs = Path(path).stat()
            self.send_response(200)
            self.send_header("Content-Type", self.guess_type(path))
            self.send_header("Content-Encoding", "br")
            self.send_header("Content-Length", str(fs.st_size))
            self.end_headers()
            return f
        return super().send_head()

    def log_message(self, fmt, *args):
        sys.stderr.write("[serve] " + (fmt % args) + "\n")


def main():
    ap = argparse.ArgumentParser(description="Serve a Conqueror web build locally")
    ap.add_argument("folder")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--open", action="store_true", help="open the game in the default browser")
    args = ap.parse_args()
    folder = Path(args.folder).resolve()
    if not (folder / "index.html").is_file():
        sys.exit(f"{folder} has no index.html (package the game with Tools/Web/package_web.py)")
    mimetypes.add_type("application/wasm", ".wasm")
    server = http.server.ThreadingHTTPServer((args.bind, args.port), functools.partial(Handler, directory=str(folder)))
    url = f"http://{args.bind if args.bind != '0.0.0.0' else 'localhost'}:{server.server_address[1]}/"
    print(f"Serving {folder} at {url} (Ctrl+C to stop)", flush=True)
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
