# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

import threading
import uuid
from typing import final

from tools.visual_testing import core
from tools.visual_testing.protocol import schema


@final
class RunManager:
    def __init__(self, exec_ctx: core.TestExecutionContext):
        self._lock = threading.Lock()
        self._base_ctx = exec_ctx
        self._run_id: str | None = None
        self._state = schema.RunState.IDLE
        self._current_test: str | None = None
        self._completed = 0
        self._test_names: list[str] = []
        self._run_ctx: core.TestExecutionContext | None = None
        self._thread: threading.Thread | None = None

    def is_running(self) -> bool:
        with self._lock:
            return self._state == schema.RunState.RUNNING

    def start(
        self,
        test_names: list[str],
        show_viewer_window: bool,
        output_dir: core.OutputDirectory,
    ) -> str:
        with self._lock:
            if self._state == schema.RunState.RUNNING:
                raise RuntimeError("A test run is already in progress")

            run_id = str(uuid.uuid4())
            # Each run works on its own copy, so its progress, cancellation and in-flight
            # process stay contained to it.
            run_ctx = self._base_ctx.for_run(
                tests=list(test_names),
                output_dir=output_dir,
                show_viewer_window=show_viewer_window,
            )

            self._run_id = run_id
            self._state = schema.RunState.RUNNING
            self._current_test = None
            self._completed = 0
            self._test_names = list(test_names)
            self._run_ctx = run_ctx

        self._thread = threading.Thread(
            target=lambda: self._worker(run_id, run_ctx, list(test_names))
        )
        self._thread.start()
        return run_id

    def status(self, run_id: str) -> schema.GetRunStatusResponse | None:
        with self._lock:
            if run_id != self._run_id:
                return None
            return schema.GetRunStatusResponse(
                state=self._state,
                current_test=self._current_test,
                completed=self._completed,
                total=len(self._test_names),
            )

    def cancel(self, run_id: str) -> bool:
        with self._lock:
            if (
                run_id != self._run_id
                or self._state != schema.RunState.RUNNING
                or self._run_ctx is None
            ):
                return False

            self._run_ctx.terminate = True
            if self._run_ctx.process is not None:
                self._run_ctx.process.terminate()
            thread = self._thread

        if thread is not None:
            thread.join()
        return True

    def _worker(
        self,
        run_id: str,
        ctx: core.TestExecutionContext,
        test_names: list[str],
    ):
        for test_name in test_names:
            with self._lock:
                if run_id != self._run_id:
                    return
                if ctx.terminate:
                    break
                self._current_test = test_name

            test = next((t for t in ctx.manifest if t.info.name == test_name), None)
            if test is None:
                with self._lock:
                    self._completed += 1
                continue

            ctx.tests = [test_name]
            ctx.report_to_append_to = (
                core.read_report(ctx.output_dir.report_path())
                if ctx.output_dir.has_report()
                else None
            )

            _ = core.run_tests_in_context(ctx)

            with self._lock:
                if run_id != self._run_id:
                    return
                self._completed += 1
                if ctx.terminate:
                    break

        with self._lock:
            if run_id == self._run_id:
                self._state = (
                    schema.RunState.CANCELLED if ctx.terminate else schema.RunState.DONE
                )
                self._current_test = None
