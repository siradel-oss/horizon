import json
import urllib.parse
import tarfile
import sys
import subprocess
import shutil
import base64

from .platforms import *
from .packaging import *
from . import lockfile
from .utils import create_temp_dir, create_temp_file

class ManifestEntry:
    def __init__(self, name: str, platforms: list[Platform] = None):
        self.name = name
        self.platforms = platforms

    def is_platform_dependent(self) -> bool:
        return self.platforms is not None

    def supports_platform(self, platform: Platform) -> bool:
        return platform in self.supported_platforms()

    def supported_platforms(self) -> list[Platform]:
        return self.platforms if self.is_platform_dependent() else Platform.all()

    def artifact_names(self) -> list[str]:
        if self.is_platform_dependent():
            return [f"{self.name}_{PLATFORM_NAME[p]}" for p in self.supported_platforms()]
        else:
            return [self.name]

    def build_package(self, dst_dir_url: str, platform: Platform) -> tuple[str, lockfile.LockEntry]:
        raise NotImplementedError("ManifestEntry.build_package() is not implemented")

class HttpArchiveManifestEntry(ManifestEntry):
    def __init__(self, name: str, url: str, prefix: str = None, patches: list[str] = None, build_file: str = None, platforms: list[Platform] = None):
        super().__init__(name, platforms)
        self.url = url
        self.prefix = prefix
        self.patches = patches
        self.build_file = build_file

    def from_dict(data: any) -> "HttpArchiveManifestEntry":
        platforms = parse_platforms(data) if "platforms" in data else None
        return HttpArchiveManifestEntry(
            name = data["name"],
            url = data["url"],
            prefix = data.get("strip_prefix", None),
            patches = data.get("patches", None),
            build_file = data.get("build_file", None),
            platforms = platforms,
        )

    def build_package(self, dst_dir_url: str, platform: Platform) -> tuple[str, lockfile.LockEntry]:
        filename = Path(urllib.parse.urlparse(self.url).path).name
        print(f"Mirror file {self.url}")
        tmp = create_temp_file()
        dgst = download_file_with_digest(self.url, tmp.path)
        new_url = upload_file_with_digest(tmp.path, dgst, dst_dir_url, filename)
        package_name = f"{self.name}_{PLATFORM_NAME[platform]}" if self.is_platform_dependent() else self.name
        return package_name, lockfile.HttpArchiveLockEntry([self.url, new_url], dgst, prefix=self.prefix, patches=self.patches, build_file=self.build_file)

class GithubManifestEntry(HttpArchiveManifestEntry):
    def __init__(self, name: str, repo: str, ref: str, prefix: str = None, patches: list[str] = None, build_file: str = None):
        only_repo = repo.split("/")[1]
        url = f"https://github.com/{repo}/archive/{ref}.tar.gz"

        if prefix is None:
            if ref[0] == "v":
                # Version tags starting with "v" are stripped of the
                # v character for the package file on GitHub...
                prefix = f"{only_repo}-{ref[1:]}"
            else:
                prefix = f"{only_repo}-{ref}"

        super().__init__(name, url, prefix=prefix, patches=patches, build_file=build_file)

    def from_dict(data: any, bazel_rules: bool = False) -> "GithubManifestEntry":
        return GithubManifestEntry(
            name = data["name"],
            repo = data["repo"],
            ref = data["ref"],
            prefix = data.get("strip_prefix", None),
            patches = data.get("patches", None),
            build_file = data.get("build_file", None),
        )

class HttpFileManifestEntry(ManifestEntry):
    def __init__(self, name: str, url: str, executable: bool, platforms: list[Platform]):
        super().__init__(name, platforms)
        self.url = url
        self.executable = executable

    def from_dict(data: any) -> "HttpFileManifestEntry":
        platforms = parse_platforms(data) if "platforms" in data else None
        return HttpFileManifestEntry(
            name = data["name"],
            url = data["url"],
            executable = data.get("executable", False),
            platforms = platforms,
        )

    def build_package(self, dst_dir_url: str, platform: Platform) -> tuple[str, lockfile.LockEntry]:
        filename = Path(urllib.parse.urlparse(self.url).path).name
        print(f"Mirror file {self.url}")
        tmp = create_temp_file()
        dgst = download_file_with_digest(self.url, tmp.path)
        new_url = upload_file_with_digest(tmp.path, dgst, dst_dir_url, filename)
        package_name = f"{self.name}_{PLATFORM_NAME[platform]}" if self.is_platform_dependent() else self.name
        return package_name, lockfile.HttpFileLockEntry([self.url, new_url], dgst, executable=self.executable)

class ExternalManifestEntry(ManifestEntry):
    def __init__(self, name: str, script: str, version: str, platforms: list[Platform]):
        super().__init__(name, platforms=platforms)
        self.script = script
        self.version = version

    def from_dict(data: any) -> "ExternalManifestEntry":
        platforms = parse_platforms(data) if "platforms" in data else None
        return ExternalManifestEntry(
            name = data["name"],
            script = data["script"],
            version = data["version"],
            platforms = platforms,
        )

    def build_local_archive(self, platform: Platform) -> Path:
        short_platform = PLATFORM_NAME[platform]
        install_dir = create_temp_dir()

        res = subprocess.run([sys.executable, self.script, self.version, short_platform, install_dir.path])
        if res.returncode != 0:
            sys.exit(res.returncode)

        package_file = create_temp_file(suffix=".tar.gz")
        with tarfile.open(package_file.path, "w:gz") as archive:
            archive.add(install_dir.path, arcname="/")

        version_hash = base64.urlsafe_b64encode(self.version.encode()).decode()
        file_name = f"prebuilt_{short_platform}.{version_hash}.tar.gz" if self.is_platform_dependent() else f"prebuilt_{version_hash}.tar.gz"
        file_path = Path("third_party") / self.name / file_name

        file_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(package_file.path, file_path)

        return file_path

    def build_package(self, dst_dir_url: str, platform: Platform) -> tuple[str, lockfile.LockEntry]:
        print(f"Making package for {self.name}")

        short_platform = PLATFORM_NAME[platform]
        local_archive = self.build_local_archive(platform)
        package_name = f"{self.name}_{short_platform}" if self.is_platform_dependent() else self.name

        bazel_package = "//" + "/".join(local_archive.parent.parts)
        bazel_archive_label = bazel_package + ":" + local_archive.name
        bazel_build_file_label = bazel_package + f":{package_name}.BUILD.bazel"

        dgst = compute_file_digest(local_archive)
        mirror_filename = f"{self.name}-{short_platform}-{self.version}.tar.gz" if self.is_platform_dependent() else f"{self.name}-{self.version}.tar.gz"
        mirror_url = upload_file_with_digest(local_archive, dgst, dst_dir_url, mirror_filename)

        return package_name, lockfile.LocalArchiveLockEntry(bazel_archive_label, build_file = bazel_build_file_label, mirror_url = mirror_url, mirror_digest=dgst)

class Manifest:
    def __init__(self, mirror_repository: str):
        self.mirror_repository = mirror_repository
        self.entries: list[ManifestEntry] = []

    def add(self, entry: ManifestEntry):
        self.entries.append(entry)

    def all_artifact_names(self):
        names = []
        for entry in self.entries:
            names.extend(entry.artifact_names())
        return names

    def build_package(self, name: str, platform: Platform) -> tuple[str, lockfile.LockEntry]:
        url = self.mirror_repository + f"horizon/3rd_party/{name}/"
        for entry in self.entries:
            if entry.name == name and entry.supports_platform(platform):
                return entry.build_package(url, platform)
        raise ValueError(f"Package {name} not found in manifest")

def parse(data: any) -> Manifest:
    man = Manifest(data["mirror_repository"])

    for entry in data["packages"]:
        type = entry["type"]
        if type == "github":
            man.add(GithubManifestEntry.from_dict(entry))
        elif type == "external":
            man.add(ExternalManifestEntry.from_dict(entry))
        elif type == "http_file":
            man.add(HttpFileManifestEntry.from_dict(entry))

    return man

def read(path: str) -> Manifest:
    with open(path, "rb") as f:
        return parse(json.load(f))
