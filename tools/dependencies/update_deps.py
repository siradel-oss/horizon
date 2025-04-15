import argparse
import requests.auth

import common.lockfile as lockfile
import common.manifest as deps_manifest
import common.bazel as deps_bazel
import common.packaging as deps_packaging
import common.platforms as deps_platforms

def synchronize(manifest, lock):
    print("Culling lock entries")
    all_artifacts = manifest.all_artifact_names()
    lock.cull(all_artifacts)

    print("Writing lock file")
    lock.write("hrz-packages.lock.json")

    print("Writing dependencies Bazel file")
    deps_bazel.write_bazel_deps(lock, "tools/bazel/deps.MODULE.bazel")

if __name__ == "__main__":
    default_platform = deps_platforms.get_current_platform()

    parser = argparse.ArgumentParser(description="Update external dependencies.")
    parser.add_argument("targets", type=str, nargs="*")
    parser.add_argument("-t", "--platform", type=str, choices=deps_platforms.PLATFORM_TRIPLE.values(), default=default_platform, required=default_platform is None)
    parser.add_argument("-u", "--user", type=str, help="Raw Nexus user", required=True)
    parser.add_argument("-p", "--password", type=str, help="Raw Nexus password", required=True)
    args = parser.parse_args()

    deps_packaging.register_nexus_auth(requests.auth.HTTPBasicAuth(args.user, args.password))
    platform = deps_platforms.TRIPLE_PLATFORM[args.platform]

    print("Parsing manifest")
    manifest = deps_manifest.read("hrz-packages.json")

    print("Parsing lock file")
    lock = lockfile.read("hrz-packages.lock.json")

    for target in args.targets:
        print(f"Updating {target} for platform {args.platform}")
        lock.add(*manifest.build_package(target, platform))

    synchronize(manifest, lock)
