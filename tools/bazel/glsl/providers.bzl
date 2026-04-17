GlslInfo = provider(
    fields = [
        "check_version",
        "version",
        "profile",
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
