# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT


class RpcError(Exception):
    def __init__(self, status_code: int, message: str):
        super().__init__(message)
        self.status_code: int = status_code
        self.message: str = message
