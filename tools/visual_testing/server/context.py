# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

from dataclasses import dataclass
from pathlib import Path

from tools.visual_testing import core
from tools.visual_testing.server.run_manager import RunManager


@dataclass
class ServerContext:
    root: Path
    manifest_path: Path
    suite_path: Path
    execution_ctx: core.TestExecutionContext
    run_manager: RunManager
