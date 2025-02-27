GlslInfo = provider(
    fields = [
        "check_version",
        "profile",
        "validator",
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
