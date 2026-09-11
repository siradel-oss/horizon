# SPDX-FileCopyrightText: Copyright 2019 Siradel
# SPDX-License-Identifier: MIT

import argparse
import os
import re
import shutil
import ssl
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from python.runfiles import Runfiles


class HrzHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 64

    def handle_error(self, request, client_address):
        exception = sys.exc_info()[1]
        if isinstance(exception, (BrokenPipeError, ConnectionResetError, TimeoutError)):
            return

        super().handle_error(request, client_address)


class HrzRequestHandler(SimpleHTTPRequestHandler):
    timeout = 30

    extensions_map = {
        "": "application/octet-stream",
        ".css": "text/css",
        ".html": "text/html",
        ".jpeg": "image/jpg",
        ".jpg": "image/jpg",
        ".js": "text/javascript",
        ".json": "application/json",
        ".manifest": "text/cache-manifest",
        ".mvt": "application/vnd.mapbox-vector-tile",
        ".png": "image/png",
        ".svg": "image/svg+xml",
        ".ttf": "font/ttf",
        ".txt": "text/plain",
        ".webp": "image/webp",
        ".woff": "font/woff",
        ".woff2": "font/woff2",
        ".wasm": "application/wasm",
        ".xml": "application/xml",
    }

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")

        super().end_headers()

    def send_head(self):
        self.range_start = 0
        self.range_end = None

        if "Range" not in self.headers:
            return super().send_head()

        path = self.translate_path(self.path)

        try:
            file = open(path, "rb")
        except FileNotFoundError:
            self.send_error(404, "File Not Found")
            return None
        except OSError as e:
            self.send_error(500, f"Internal Server Error: {e}")
            return None

        keep_file_open = False
        try:
            file_size = os.fstat(file.fileno()).st_size

            # Parse the Range header
            range_header = self.headers["Range"]
            range_match = re.match(r"bytes=(\d+)-(\d*)", range_header)
            if not range_match:
                self.send_error(400, "Invalid Range Header")
                return None

            start = int(range_match.group(1))
            end = range_match.group(2)
            end = int(end) if end else file_size - 1

            if start >= file_size or end >= file_size or start > end:
                self.send_error(416, "Requested Range Not Satisfiable")
                return None

            self.send_response(206)
            self.send_header("Content-Type", self.guess_type(path))
            self.send_header("Content-Range", f"bytes {start}-{end}/{file_size}")
            self.send_header("Content-Length", str(end - start + 1))
            self.end_headers()

            self.range_start = start
            self.range_end = end

            file.seek(start)

            keep_file_open = True
            return file
        except Exception as e:
            self.send_error(500, f"Internal Server Error: {e}")
            return None
        finally:
            if not keep_file_open:
                file.close()

    def copyfile(self, source, outputfile):
        try:
            if self.range_end is not None:
                remaining = self.range_end - self.range_start + 1
                while remaining > 0:
                    chunk = source.read(min(remaining, 256 * 1024))
                    if not chunk:
                        break
                    outputfile.write(chunk)
                    remaining -= len(chunk)
            else:
                shutil.copyfileobj(source, outputfile)
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True


if __name__ == "__main__":
    r = Runfiles.Create()
    default_cert_path = r.Rlocation("horizon/tools/http_server/cert.pem")
    default_keyfile_path = r.Rlocation("horizon/tools/http_server/key.pem")

    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--port", help="Port", type=int, default=8080)
    parser.add_argument(
        "-n", "--hostname", help="Hostname", type=str, default="0.0.0.0"
    )
    parser.add_argument(
        "-d", "--directory", help="Path of the directory to serve", type=str, default=""
    )
    parser.add_argument("-t", "--tls", help="Use TLS", action="store_true")
    parser.add_argument(
        "--certificate",
        help="Certificate path",
        type=str,
        default=default_cert_path,
    )
    parser.add_argument(
        "--keyfile",
        help="Certificate private keyfile path",
        type=str,
        default=default_keyfile_path,
    )

    args = parser.parse_args(sys.argv[1:])

    port = args.port
    hostname = args.hostname
    protocol = "https" if args.tls else "http"

    if args.directory:
        os.chdir(args.directory)

    httpd = HrzHTTPServer((hostname, port), HrzRequestHandler)

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

    print(f"Serving on {protocol}://{hostname}:{port}/")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
