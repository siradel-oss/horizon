import os
import sys

from http.server import HTTPServer, SimpleHTTPRequestHandler, test

class HrzRequestHandler(SimpleHTTPRequestHandler):
    extensions_map={
        '': 'application/octet-stream',
        '.css': 'text/css',
        '.html': 'text/html',
        '.jpeg': 'image/jpg',
        '.jpg': 'image/jpg',
        '.js': 'text/javascript',
        '.json': 'application/json',
        '.manifest': 'text/cache-manifest',
        '.mvt': 'application/vnd.mapbox-vector-tile',
        '.png': 'image/png',
        '.svg': 'image/svg+xml',
        '.ttf': 'font/ttf',
        '.txt': 'text/plain',
        '.webp': 'image/webp',
        '.woff': 'font/woff',
        '.woff2': 'font/woff2',
        '.wasm': 'application/wasm',
        '.xml': 'application/xml',
    }

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")

        SimpleHTTPRequestHandler.end_headers(self)

if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080

    if len(sys.argv) > 2:
        web_dir = sys.argv[2]
        os.chdir(web_dir)

    test(HrzRequestHandler, HTTPServer, port=port, bind="0.0.0.0")
