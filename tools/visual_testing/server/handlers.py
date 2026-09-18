# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

import base64
import shutil
from collections.abc import Callable
from typing import Any

from tools.visual_testing import core
from tools.visual_testing.protocol import schema
from tools.visual_testing.server.context import ServerContext
from tools.visual_testing.server.errors import RpcError


def _find_test(manifest: list[core.Test], name: str) -> core.Test | None:
    return next((t for t in manifest if t.info.name == name), None)


def _to_test_info(test: core.Test) -> schema.TestInfo:
    return schema.TestInfo(
        **test.info.model_dump(),
        has_input=test.input_path().exists(),
        has_reference_image=test.ref_image_path().exists(),
    )


def _drop_results(output_dir: core.OutputDirectory, names: set[str]):
    if not names or not output_dir.has_report():
        return

    report = core.read_report(output_dir.report_path())
    core.remove_results(report, names)
    core.write_json_report(report, output_dir.report_path())


def _read_current_report(output_dir: core.OutputDirectory) -> schema.Report | None:
    if not output_dir.has_report():
        return None
    return core.read_report(output_dir.report_path())


def _save_uploaded_input(test: core.Test, content_base64: str):
    dest_path = test.input_path()
    dest_path.absolute().parent.mkdir(parents=True, exist_ok=True)
    with open(dest_path, "wb") as f:
        _ = f.write(base64.b64decode(content_base64))


def list_tests(
    _req: schema.ListTestsRequest, ctx: ServerContext
) -> schema.ListTestsResponse:
    return schema.ListTestsResponse(
        tests=[_to_test_info(t) for t in ctx.execution_ctx.manifest]
    )


def create_test(
    req: schema.CreateTestRequest, ctx: ServerContext
) -> schema.CreateTestResponse:
    if _find_test(ctx.execution_ctx.manifest, req.definition.name) is not None:
        raise RpcError(409, f"A test named '{req.definition.name}' already exists")

    test = core.Test(ctx.suite_path)
    test.info = req.definition

    _save_uploaded_input(test, req.input_file_content_base64)

    ctx.execution_ctx.manifest.append(test)
    core.write_manifest(ctx.execution_ctx.manifest, ctx.manifest_path)

    if req.generate_reference:
        _ = core.generate_ref_image(
            ctx.execution_ctx.viewer, test, ctx.execution_ctx.output_dir
        )

    return schema.CreateTestResponse(test=_to_test_info(test))


def update_test(
    req: schema.UpdateTestRequest, ctx: ServerContext
) -> schema.UpdateTestResponse:
    test = _find_test(ctx.execution_ctx.manifest, req.current_name)
    if test is None:
        raise RpcError(404, f"No test named '{req.current_name}'")

    if req.new_definition.name != req.current_name:
        existing = _find_test(ctx.execution_ctx.manifest, req.new_definition.name)
        if existing is not None:
            raise RpcError(
                409, f"A test named '{req.new_definition.name}' already exists"
            )

    old_input_path = test.input_path()
    old_ref_image_path = test.ref_image_path()

    test.info = req.new_definition

    if req.input_file_content_base64 is not None:
        _save_uploaded_input(test, req.input_file_content_base64)
        if old_input_path.exists():
            old_input_path.unlink()
        if old_ref_image_path.exists():
            old_ref_image_path.unlink()
    elif req.new_definition.name != req.current_name:
        if old_input_path.exists():
            new_input_path = test.input_path()
            _ = shutil.move(str(old_input_path), str(new_input_path))
        if old_ref_image_path.exists():
            new_ref_image_path = test.ref_image_path()
            _ = shutil.move(str(old_ref_image_path), str(new_ref_image_path))

    core.write_manifest(ctx.execution_ctx.manifest, ctx.manifest_path)

    if req.generate_reference:
        _ = core.generate_ref_image(
            ctx.execution_ctx.viewer, test, ctx.execution_ctx.output_dir
        )

    return schema.UpdateTestResponse(test=_to_test_info(test))


def delete_test(
    req: schema.DeleteTestRequest, ctx: ServerContext
) -> schema.DeleteTestResponse:
    test = _find_test(ctx.execution_ctx.manifest, req.name)
    if test is None:
        raise RpcError(404, f"No test named '{req.name}'")

    ctx.execution_ctx.manifest.remove(test)
    core.write_manifest(ctx.execution_ctx.manifest, ctx.manifest_path)
    return schema.DeleteTestResponse()


def get_test(req: schema.GetTestRequest, ctx: ServerContext) -> schema.GetTestResponse:
    test = _find_test(ctx.execution_ctx.manifest, req.name)
    if test is None:
        raise RpcError(404, f"No test named '{req.name}'")

    latest_result = None
    report = _read_current_report(ctx.execution_ctx.output_dir)
    if report is not None:
        for result in report.results:
            if result.name == test.info.name:
                latest_result = result
                break

    return schema.GetTestResponse(test=_to_test_info(test), latest_result=latest_result)


def regenerate_reference_image(
    req: schema.RegenerateReferenceImageRequest, ctx: ServerContext
) -> schema.RegenerateReferenceImageResponse:
    if ctx.run_manager.is_running():
        raise RpcError(
            409, "Cannot regenerate reference images while a run is in progress"
        )

    test = _find_test(ctx.execution_ctx.manifest, req.name)
    if test is None:
        raise RpcError(404, f"No test named '{req.name}'")

    if not core.refresh_ref_image(
        ctx.execution_ctx.viewer, test, ctx.execution_ctx.output_dir
    ):
        raise RpcError(
            500,
            f"Failed to update the reference image for '{req.name}' "
            + "(see the server console for details)",
        )

    _drop_results(ctx.execution_ctx.output_dir, {req.name})

    return schema.RegenerateReferenceImageResponse()


def bulk_regenerate_reference_images(
    req: schema.BulkRegenerateReferenceImagesRequest,
    ctx: ServerContext,
) -> schema.BulkRegenerateReferenceImagesResponse:
    if ctx.run_manager.is_running():
        raise RpcError(
            409, "Cannot regenerate reference images while a run is in progress"
        )

    names = set(req.names)
    regenerated: set[str] = set()
    failed: list[str] = []

    for test in ctx.execution_ctx.manifest:
        if test.info.name not in names:
            continue
        if core.refresh_ref_image(
            ctx.execution_ctx.viewer, test, ctx.execution_ctx.output_dir
        ):
            regenerated.add(test.info.name)
        else:
            failed.append(test.info.name)

    # Only the ones that actually got a new reference lose their result; the others still
    # describe the reference they were compared against.
    _drop_results(ctx.execution_ctx.output_dir, regenerated)

    if failed:
        raise RpcError(
            500,
            f"Failed to update {len(failed)} reference image(s): "
            + ", ".join(sorted(failed))
            + " (see the server console for details)",
        )

    return schema.BulkRegenerateReferenceImagesResponse()


def get_report(
    _req: schema.GetReportRequest, ctx: ServerContext
) -> schema.GetReportResponse:
    report = _read_current_report(ctx.execution_ctx.output_dir)
    if report is None:
        return schema.GetReportResponse(has_report=False, report=None)
    return schema.GetReportResponse(has_report=True, report=report)


def get_workdir(
    _req: schema.GetWorkdirRequest, ctx: ServerContext
) -> schema.GetWorkdirResponse:
    wd = ctx.execution_ctx.output_dir
    return schema.GetWorkdirResponse(
        path=str(wd.path), is_temp=not wd.preserve, has_report=wd.has_report()
    )


def set_workdir(
    req: schema.SetWorkdirRequest, ctx: ServerContext
) -> schema.SetWorkdirResponse:
    if ctx.run_manager.is_running():
        raise RpcError(
            409, "Cannot change the working directory while a run is in progress"
        )

    output_dir = core.OutputDirectory(req.path)
    ctx.execution_ctx.output_dir = output_dir

    return schema.SetWorkdirResponse(
        path=str(output_dir.path),
        is_temp=not output_dir.preserve,
        has_report=output_dir.has_report(),
    )


def start_run(
    req: schema.StartRunRequest, ctx: ServerContext
) -> schema.StartRunResponse:
    if ctx.run_manager.is_running():
        raise RpcError(409, "A test run is already in progress")
    run_id = ctx.run_manager.start(
        req.test_names, req.show_viewer_window, ctx.execution_ctx.output_dir
    )
    return schema.StartRunResponse(run_id=run_id)


def get_run_status(
    req: schema.GetRunStatusRequest, ctx: ServerContext
) -> schema.GetRunStatusResponse:
    status = ctx.run_manager.status(req.run_id)
    if status is None:
        raise RpcError(404, f"No run with id '{req.run_id}'")
    return status


def cancel_run(
    req: schema.CancelRunRequest, ctx: ServerContext
) -> schema.CancelRunResponse:
    _ = ctx.run_manager.cancel(req.run_id)
    return schema.CancelRunResponse()


def sync_manifest(
    _req: schema.SyncManifestRequest, ctx: ServerContext
) -> schema.SyncManifestResponse:
    ctx.execution_ctx.manifest = core.sync_manifest(
        ctx.execution_ctx.manifest, ctx.suite_path, ctx.manifest_path
    )
    return schema.SyncManifestResponse()


def ping(_req: schema.PingRequest, _ctx: ServerContext) -> schema.PingResponse:
    return schema.PingResponse()


HANDLERS: dict[str, Callable[[Any, ServerContext], schema.Model]] = {
    "ListTests": list_tests,
    "CreateTest": create_test,
    "UpdateTest": update_test,
    "DeleteTest": delete_test,
    "GetTest": get_test,
    "RegenerateReferenceImage": regenerate_reference_image,
    "BulkRegenerateReferenceImages": bulk_regenerate_reference_images,
    "GetReport": get_report,
    "GetWorkdir": get_workdir,
    "SetWorkdir": set_workdir,
    "StartRun": start_run,
    "GetRunStatus": get_run_status,
    "CancelRun": cancel_run,
    "SyncManifest": sync_manifest,
    "Ping": ping,
}
