REM     SPDX-FileCopyrightText: Copyright 2026 Siradel
REM     SPDX-License-Identifier: MIT

@echo off

for /f "tokens=*" %%i in ('git describe --always --abbrev^=40 --long --dirty 2^>nul') do set COMMIT_HASH=%%i

if not "%COMMIT_HASH%"=="" (
    echo STABLE_GIT_COMMIT %COMMIT_HASH%
) else (
    echo STABLE_GIT_COMMIT unknown
)

