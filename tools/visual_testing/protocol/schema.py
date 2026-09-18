# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

from enum import IntEnum, StrEnum
from typing import ClassVar

from pydantic import BaseModel, ConfigDict
from pydantic.alias_generators import to_camel


class Model(BaseModel):
    model_config: ClassVar[ConfigDict] = ConfigDict(
        alias_generator=to_camel,
        populate_by_name=True,
        extra="forbid",
        validate_assignment=True,
    )


class TestType(IntEnum):
    HRZ_SCENE = 0
    MAPBOX_STYLE = 1


class ErrorStrategy(IntEnum):
    ABORT = 0
    MESSAGE = 1


class ErrorType(IntEnum):
    NONE = 0
    VIEWER = 1
    TIMEOUT = 2
    THRESHOLD = 3
    MISSING_REF = 4
    MISSING_INPUT = 5
    MIGRATION_ERROR = 6
    ABORTED = 7
    COMPARATOR = 8


class RunState(StrEnum):
    IDLE = "idle"
    RUNNING = "running"
    DONE = "done"
    CANCELLED = "cancelled"


class TestDefinition(Model):
    name: str
    type: TestType
    error_threshold: float
    timeout: int
    error_strategy: ErrorStrategy
    error_message: str = ""


class TestInfo(TestDefinition):
    has_input: bool
    has_reference_image: bool


class Result(Model):
    name: str
    type: TestType
    success: bool
    error_ratio: float | None = None
    error_type: ErrorType
    error_message: str = ""
    log: str | None = None
    duration: float
    date: str


class Report(Model):
    branch: str
    commit: str
    date: str
    results: list[Result]
    duration: float


class ListTestsRequest(Model):
    pass


class ListTestsResponse(Model):
    tests: list[TestInfo]


class CreateTestRequest(Model):
    definition: TestDefinition
    # Raw bytes of the input file, base64-encoded (test fixtures are small; this keeps the file
    # upload on the same generic JSON RPC endpoint as everything else).
    input_file_content_base64: str
    generate_reference: bool = False


class CreateTestResponse(Model):
    test: TestInfo


class UpdateTestRequest(Model):
    current_name: str
    new_definition: TestDefinition
    # None here keeps the existing input file.
    input_file_content_base64: str | None = None
    generate_reference: bool = False


class UpdateTestResponse(Model):
    test: TestInfo


class DeleteTestRequest(Model):
    name: str


class DeleteTestResponse(Model):
    pass


class GetTestRequest(Model):
    name: str


class GetTestResponse(Model):
    test: TestInfo
    latest_result: Result | None = None


class RegenerateReferenceImageRequest(Model):
    name: str


class RegenerateReferenceImageResponse(Model):
    pass


class BulkRegenerateReferenceImagesRequest(Model):
    names: list[str]


class BulkRegenerateReferenceImagesResponse(Model):
    pass


class GetReportRequest(Model):
    pass


class GetReportResponse(Model):
    has_report: bool
    report: Report | None = None


class GetWorkdirRequest(Model):
    pass


class GetWorkdirResponse(Model):
    path: str
    is_temp: bool
    has_report: bool


class SetWorkdirRequest(Model):
    path: str


class SetWorkdirResponse(Model):
    path: str
    is_temp: bool
    has_report: bool


class StartRunRequest(Model):
    test_names: list[str]
    show_viewer_window: bool = False


class StartRunResponse(Model):
    run_id: str


class GetRunStatusRequest(Model):
    run_id: str


class GetRunStatusResponse(Model):
    state: RunState
    current_test: str | None = None
    completed: int
    total: int


class CancelRunRequest(Model):
    run_id: str


class CancelRunResponse(Model):
    pass


class SyncManifestRequest(Model):
    pass


class SyncManifestResponse(Model):
    pass


class PingRequest(Model):
    pass


class PingResponse(Model):
    pass


class ErrorResponse(Model):
    message: str


METHODS: dict[str, tuple[type[Model], type[Model]]] = {
    "ListTests": (ListTestsRequest, ListTestsResponse),
    "CreateTest": (CreateTestRequest, CreateTestResponse),
    "UpdateTest": (UpdateTestRequest, UpdateTestResponse),
    "DeleteTest": (DeleteTestRequest, DeleteTestResponse),
    "GetTest": (GetTestRequest, GetTestResponse),
    "RegenerateReferenceImage": (
        RegenerateReferenceImageRequest,
        RegenerateReferenceImageResponse,
    ),
    "BulkRegenerateReferenceImages": (
        BulkRegenerateReferenceImagesRequest,
        BulkRegenerateReferenceImagesResponse,
    ),
    "GetReport": (GetReportRequest, GetReportResponse),
    "GetWorkdir": (GetWorkdirRequest, GetWorkdirResponse),
    "SetWorkdir": (SetWorkdirRequest, SetWorkdirResponse),
    "StartRun": (StartRunRequest, StartRunResponse),
    "GetRunStatus": (GetRunStatusRequest, GetRunStatusResponse),
    "CancelRun": (CancelRunRequest, CancelRunResponse),
    "SyncManifest": (SyncManifestRequest, SyncManifestResponse),
    "Ping": (PingRequest, PingResponse),
}
