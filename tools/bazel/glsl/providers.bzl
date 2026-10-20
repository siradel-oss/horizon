# SPDX-FileCopyrightText: Copyright 2025 Siradel
# SPDX-License-Identifier: MIT

GlslInfo = provider(
    fields = [
        "check_version",
        "version",
        "profile",
        "assembler",
        "validator",
        "compressor",
    ],
)

GlslConfigInfo = provider(
    fields = [
        "defines",
        "includes",
        "common",
    ],
)

GlslProgramInfo = provider(
    fields = [
        "name",
        "vert",
        "frag",
    ],
)
