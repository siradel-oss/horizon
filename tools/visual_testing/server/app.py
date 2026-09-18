# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

import platform
import subprocess
import threading
import traceback
import webbrowser
from collections.abc import Callable
from pathlib import Path

from flask import (
    Flask,
    Response,
    abort,
    jsonify,
    request,
    send_file,
    send_from_directory,
)
from pydantic import ValidationError
from werkzeug.exceptions import NotFound

from tools.visual_testing import core
from tools.visual_testing.protocol import schema
from tools.visual_testing.server import handlers
from tools.visual_testing.server.context import ServerContext
from tools.visual_testing.server.errors import RpcError

IMAGE_KIND_PATHS: dict[str, Callable[[core.Test, core.OutputDirectory], Path]] = {
    "reference": lambda test, output_dir: test.ref_image_path(),
    "capture": lambda test, output_dir: output_dir.capture_path(test),
    "diff": lambda test, output_dir: output_dir.diff_path(test),
    "expected": lambda test, output_dir: output_dir.expected_path(test),
}


def create_app(client_dist_dir: Path | None, ctx: ServerContext):
    app = Flask(__name__, static_folder=None)

    def _error(status_code: int, message: str) -> tuple[Response, int]:
        return (
            jsonify(schema.ErrorResponse(message=message).model_dump(by_alias=True)),
            status_code,
        )

    @app.post("/rpc/<method>")
    def call(method: str):
        entry = schema.METHODS.get(method)
        handler = handlers.HANDLERS.get(method)
        if entry is None or handler is None:
            return _error(404, f"Unknown method '{method}'")

        request_cls, _response_cls = entry

        try:
            payload = request.get_json(force=True, silent=True) or {}
            req = request_cls.model_validate(payload)
        except ValidationError as e:
            return _error(400, str(e))

        try:
            resp = handler(req, ctx)
        except RpcError as e:
            return _error(e.status_code, e.message)
        except Exception as e:
            traceback.print_exc()
            return _error(500, str(e))

        return jsonify(resp.model_dump(mode="json", by_alias=True))

    @app.get("/images/<test_name>/<kind>")
    def get_image(test_name: str, kind: str):
        path_fn = IMAGE_KIND_PATHS.get(kind)
        if path_fn is None:
            abort(404)

        test = next(
            (t for t in ctx.execution_ctx.manifest if t.info.name == test_name), None
        )
        if test is None:
            abort(404)

        image_path = path_fn(test, ctx.execution_ctx.output_dir)
        if not image_path.exists():
            abort(404)

        return send_file(image_path, mimetype="image/png")

    if client_dist_dir is not None:

        @app.get("/", defaults={"req_path": ""})
        @app.get("/<path:req_path>")
        def serve_client(req_path: Path):
            try:
                return send_from_directory(client_dist_dir, req_path)
            except NotFound:
                return send_from_directory(client_dist_dir, "index.html")

    return app


def _build_client(root: Path) -> Path:
    print("Building web client...", end="", flush=True)
    config = "wasm_" + platform.system().lower()
    build_error = (
        subprocess.run(
            [
                "bazel",
                "build",
                "//tools/visual_testing/client:build",
                "--config=" + config,
            ],
            cwd=root,
            check=False,
        ).returncode
        != 0
    )
    if build_error:
        print(" [FAILED]")
        raise RuntimeError(
            "Failed to build the web client (try --client-dev during frontend development)"
        )
    print(" [OK]")
    return root / "bazel-bin" / "tools" / "visual_testing" / "client" / "dist"


def run(
    server_ctx: ServerContext,
    port: int,
    open_browser: bool,
    build_client: bool,
):
    client_dist_dir = _build_client(server_ctx.root) if build_client else None
    app = create_app(client_dist_dir, server_ctx)

    url = f"http://127.0.0.1:{port}/"
    if not client_dist_dir:
        print(
            "--client-dev: not serving the web client here, run its Vite dev server separately "
            + "(bazel run //tools/visual_testing/client:vite) and proxy /rpc + /images to this server."
        )
    if open_browser:
        threading.Timer(1.0, lambda: webbrowser.open(url)).start()

    print(f"Serving visual testing GUI at {url}")
    app.run(host="127.0.0.1", port=port, threaded=True)
