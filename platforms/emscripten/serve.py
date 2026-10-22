#!/usr/bin/env python3
"""Serves a Warzone 2100 web build for local testing.

The web build uses threads, which need SharedArrayBuffer. Browsers give a page
SharedArrayBuffer only if the page is cross-origin isolated, so this server sends
the two headers that isolate it. (python -m http.server does not, and the page
then reports "Unsupported Browser" with "Wasm:Threads".)

Example:
    python3 platforms/emscripten/serve.py build_web/src
Then open http://localhost:8000/ in a browser.
"""

import argparse
import functools
import http.server
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parents[2]

class CrossOriginIsolatedHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        '.wasm': 'application/wasm',
    }

    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description='Serve a Warzone 2100 web build with the headers that threads need.')
    parser.add_argument('directory', help='folder that has index.html (for example, build_web/src)', default=(ROOT_DIR / 'build_web' / 'src'))
    parser.add_argument('--port', type=int, default=8000, help='port (default: 8000)')
    parser.add_argument('--bind', default='127.0.0.1', help='address to listen on (default: 127.0.0.1)')
    args = parser.parse_args()

    handler = functools.partial(CrossOriginIsolatedHandler, directory=args.directory)
    with http.server.ThreadingHTTPServer((args.bind, args.port), handler) as server:
        print(f'Serving {args.directory} at http://{args.bind}:{args.port}/')
        server.serve_forever()


if __name__ == '__main__':
    main()
