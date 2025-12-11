import json
from dataclasses import dataclass


class LockEntry:
    def serialize(self) -> dict:
        return dict(
            {k: v for k, v in vars(self).items() if v is not None}, type=self.tag
        )


class HttpArchiveLockEntry(LockEntry):
    tag: str = "http_archive"

    def __init__(
        self,
        urls: list[str],
        digest: str,
        prefix: str = None,
        patches: list[str] = None,
        build_file: str = None,
    ):
        self.urls = urls
        self.digest = digest
        self.prefix = prefix
        self.patches = patches
        self.build_file = build_file

    def from_dict(data: dict) -> "HttpArchiveLockEntry":
        return HttpArchiveLockEntry(
            urls=data["urls"],
            digest=data["digest"],
            prefix=data.get("prefix", None),
            patches=data.get("patches", None),
            build_file=data.get("build_file", None),
        )

    def as_bazel_rule(self, name: str) -> str:
        text = f"""http_archive(
    name = "{name}",
    urls = [\n"""
        text += "".join([f'        "{u}",\n' for u in self.urls])
        text += f"    ],\n"
        text += f'    sha256 = "{self.digest}",\n'

        if self.build_file:
            text += f'    build_file = "{self.build_file}",\n'

        if self.prefix:
            text += f'    strip_prefix = "{self.prefix}",\n'

        if self.patches:
            text += "    patches = [\n"
            text += "".join(
                [f'        "//third_party:patches/{p}",\n' for p in self.patches]
            )
            text += "    ],\n"

        text += ")\n"
        return text


class HttpFileLockEntry(LockEntry):
    tag: str = "http_file"

    def __init__(self, urls: list[str], digest: str, executable: bool):
        self.urls = urls
        self.digest = digest
        self.executable = executable

    def from_dict(data: dict) -> "HttpFileLockEntry":
        return HttpFileLockEntry(
            urls=data["urls"],
            digest=data["digest"],
            executable=data.get("executable", False),
        )

    def as_bazel_rule(self, name: str) -> str:
        text = f"""http_file(
    name = "{name}",
    urls = [\n"""
        text += "".join([f'        "{u}",\n' for u in self.urls])
        text += f"    ],\n"
        text += f'    sha256 = "{self.digest}",\n'

        if self.executable:
            text += "    executable = True,\n"

        text += ")\n"
        return text


class LocalArchiveLockEntry(LockEntry):
    tag: str = "local_archive"

    def __init__(
        self,
        label: str,
        mirror_url: str = None,
        mirror_digest: str = None,
        build_file: str = None,
    ):
        self.label = label
        self.build_file = build_file
        self.mirror_url = mirror_url
        self.mirror_digest = mirror_digest

    def from_dict(data: dict) -> "LocalArchiveLockEntry":
        return LocalArchiveLockEntry(
            label=data["label"],
            mirror_url=data.get("mirror_url", None),
            mirror_digest=data.get("mirror_digest", None),
            build_file=data.get("build_file", None),
        )

    def as_bazel_rule(self, name: str) -> str:
        text = f"""local_archive(
    name = "{name}",
    src = "{self.label}",\n"""

        if self.build_file:
            text += f'    build_file = "{self.build_file}",\n'

        if self.mirror_url or self.mirror_digest:
            text += f'    mirror_url = "{self.mirror_url}",\n'
            text += f'    mirror_digest = "{self.mirror_digest}",\n'

        text += ")\n"
        return text


class Lockfile:
    def __init__(self):
        self.entries: dict[str, LockEntry] = {}

    def add(self, name: str, entry: LockEntry):
        self.entries[name] = entry

    def cull(self, to_keep: list[str]):
        self.entries = {k: v for k, v in self.entries.items() if k in to_keep}

    def serialize(self) -> dict:
        return {k: v.serialize() for k, v in self.entries.items()}

    def write(self, path: str):
        with open(path, "wb+") as f:
            f.write(
                bytes(
                    json.dumps(self.serialize(), sort_keys=True, indent=2) + "\n",
                    encoding="utf8",
                )
            )


def parse(data: any) -> Lockfile:
    classes = [HttpArchiveLockEntry, HttpFileLockEntry, LocalArchiveLockEntry]
    class_lookup = {cls.tag: cls for cls in classes}
    lock = Lockfile()
    for name, prps in data.items():
        cls = class_lookup.get(prps["type"], None)
        if cls:
            lock.add(name, cls.from_dict(prps))
        else:
            raise ValueError(f"Unknown dependency type: {prps['type']}")
    return lock


def read(path: str) -> Lockfile:
    with open(path, "rb") as f:
        return parse(json.load(f))
