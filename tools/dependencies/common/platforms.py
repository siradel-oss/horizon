import enum
import platform as py_platform


class Platform(enum.Enum):
    WINDOWS = 0
    LINUX = 1
    WASM = 2

    def all() -> list["Platform"]:
        return [Platform.WINDOWS, Platform.LINUX, Platform.WASM]


PLATFORM_TRIPLE = {
    Platform.WINDOWS: "x86_64-pc-windows-msvc",
    Platform.LINUX: "x86_64-pc-linux-gnu",
    Platform.WASM: "wasm32-unknown-emscripten",
}

TRIPLE_PLATFORM = {v: k for k, v in PLATFORM_TRIPLE.items()}

PLATFORM_NAME = {
    Platform.WINDOWS: "windows",
    Platform.LINUX: "linux",
    Platform.WASM: "wasm",
}

PY_PLATFORM_TO_ID = {
    "Windows": Platform.WINDOWS,
    "Linux": Platform.LINUX,
}


def get_current_platform() -> Platform:
    if py_platform.system() in PY_PLATFORM_TO_ID:
        return PLATFORM_TRIPLE[PY_PLATFORM_TO_ID[py_platform.system()]]
    else:
        return None


def parse_platforms(data: any) -> list[Platform]:
    platforms = [TRIPLE_PLATFORM[p] for p in data.get("platforms", [])]
    return platforms if len(platforms) > 0 else Platform.all()
