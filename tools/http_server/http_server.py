import argparse
import os
import re
import shutil
import ssl
import sys

from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

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
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        self.send_header('Pragma', 'no-cache')
        self.send_header('Expires', '0')

        super().end_headers()

    def send_head(self):
        self.range_start = 0
        self.range_end = None

        if 'Range' in self.headers:
            try:
                path = self.translate_path(self.path)
                file = open(path, 'rb')
                file_size = os.path.getsize(path)

                # Parse the Range header
                range_header = self.headers['Range']
                range_match = re.match(r'bytes=(\d+)-(\d*)', range_header)
                if range_match:
                    start = int(range_match.group(1))
                    end = range_match.group(2)
                    end = int(end) if end else file_size - 1

                    if start >= file_size or end >= file_size or start > end:
                        self.send_error(416, 'Requested Range Not Satisfiable')
                        return None

                    self.send_response(206)
                    self.send_header('Content-Type', self.guess_type(path))
                    self.send_header('Content-Range', f'bytes {start}-{end}/{file_size}')
                    self.send_header('Content-Length', str(end - start + 1))
                    self.end_headers()

                    self.range_start = start
                    self.range_end = end

                    file.seek(start)
                    return file
                else:
                    self.send_error(400, 'Invalid Range Header')
                    return None
            except FileNotFoundError:
                self.send_error(404, 'File Not Found')
                return None
            except Exception as e:
                self.send_error(500, f'Internal Server Error: {e}')
                return None
        else:
            return super().send_head()

    def copyfile(self, source, outputfile):
        if self.range_end is not None:
            outputfile.write(source.read(self.range_end - self.range_start + 1))
        else:
            shutil.copyfileobj(source, outputfile)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--port", help="Port", type=int, default=8080)
    parser.add_argument("-n", "--hostname", help="Hostname", type=str, default="0.0.0.0")
    parser.add_argument("-d", "--directory", help="Path of the directory to serve", type=str, default="")
    parser.add_argument("-t", "--tls", help="Use TLS", action="store_true")
    parser.add_argument("--certificate", help="Certificate path", type=str, default=Path(__file__).parent / "cert.pem")
    parser.add_argument("--keyfile", help="Certificate private keyfile path", type=str, default=Path(__file__).parent / "key.pem")

    args = parser.parse_args(sys.argv[1:])

    port = args.port
    hostname = args.hostname

    if args.directory:
        os.chdir(args.directory)

    httpd = HTTPServer((hostname, port), HrzRequestHandler)

    if args.tls:
        cert_path = None
        if args.certificate and os.path.exists(args.certificate):
            cert_path = args.certificate
            print(f"Using certificate {cert_path}")
        else:
            print("No certificate found!")

        keyfile_path = None
        if args.keyfile and os.path.exists(args.keyfile):
            keyfile_path = args.keyfile
            print(f"Using keyfile {keyfile_path}")
        else:
            print("No keyfile found!")

        ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_context.load_cert_chain(cert_path, keyfile=keyfile_path)
        httpd.socket = ssl_context.wrap_socket(
            httpd.socket,
            server_side=True,
        )

    print(f'Serving on https://{hostname}:{port}/')
    httpd.serve_forever()
