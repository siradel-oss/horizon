import argparse
from pathlib import Path

import common.platforms as deps_platforms
import common.manifest as deps_manifest
import common.lockfile as deps_lockfile
from common.packaging import download_file_with_digest

def full_label_to_path(label: str) -> Path:
    if label.startswith("//"):
        label = label[2:]
    return Path("/".join(label.split(":")))

if __name__ == "__main__":
    default_platform = deps_platforms.get_current_platform()

    parser = argparse.ArgumentParser(description="Fetch or build prebuilt dependencies.")
    parser.add_argument("targets", help="Leave empty for all", type=str, nargs="*")
    parser.add_argument("-t", "--platform", type=str, choices=deps_platforms.PLATFORM_TRIPLE.values(), default=default_platform, required=default_platform is None)

    args = parser.parse_args()
    print("Fetching or building dependencies for platform", args.platform)

    manifest = deps_manifest.read("hrz-packages.json")
    lockfile = deps_lockfile.read("hrz-packages.lock.json")

    platform = deps_platforms.TRIPLE_PLATFORM[args.platform]

    external_deps = [dep for dep in manifest.entries if isinstance(dep, deps_manifest.ExternalManifestEntry)]
    external_deps = [dep for dep in external_deps if dep.supports_platform(platform)]

    if len(args.targets) > 0:
        external_deps = [dep for dep in external_deps if dep.name in args.targets]

    if len(external_deps) == 0:
        print("No dependencies to fetch or build")
        exit(0)

    platform_name = deps_platforms.PLATFORM_NAME[platform]

    for dep in external_deps:
        lock_name = dep.name
        if dep.is_platform_dependent():
            lock_name = f"{dep.name}_{platform_name}"

        lock_entry = lockfile.entries.get(lock_name)
        if not lock_entry or not isinstance(lock_entry, deps_lockfile.LocalArchiveLockEntry):
            raise RuntimeError(f"Missing lock entry or wrong type for {lock_name}")

        print(f"Fetching or building {lock_name}")
        local_path = full_label_to_path(lock_entry.label)
        if local_path.exists():
            print(f"{local_path} already exists, skipping")
            continue

        if lock_entry.mirror_url:
            try:
                dgst = download_file_with_digest(lock_entry.mirror_url, local_path)
                if dgst == lock_entry.mirror_digest:
                    print(f"Downloaded {lock_entry.mirror_url} to {local_path}")
                    continue
            except Exception as e:
                print(f"Failed to download {lock_entry.mirror_url}: {e}")

        print(f"Attempting to build {lock_name}")
        path = dep.build_local_archive(platform)

        if path != local_path:
            raise RuntimeError(f"Expected {path} to be {local_path}")
