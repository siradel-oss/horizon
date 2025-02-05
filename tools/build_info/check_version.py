import re
import sys

# Check that the version in version.bzl is the same as
# the one passed as the first parameter to this script.
# Then check if this is a build for a release tag, by
# inspecting the second parameter. In case of a release,
# verify that the tag name corresponds to the version,
# and that we're not attempting to release a snapshot
# commit. If not a release tag, check that the commit
# isn't a release commit.

if (len(sys.argv) < 3):
    print("Usage: %s <version> <ci_commit_ref_name>" % sys.argv[0])
    sys.exit(1)

version = sys.argv[1]
ref_name = sys.argv[2]

with open("version.bzl", 'r') as version_file:
    version_file_string = version_file.read().split("\n")[0].rstrip()

    if version_file_string != "HRZ_VERSION = \"%s\"" % version:
        print("Error: Inconsistent version strings")
        sys.exit(2)

    if re.match("^v\d+.*$", ref_name):
        # This is a release build.

        if ("SNAPSHOT" in version):
            print("Error: Trying to release a SNAPSHOT commit")
            sys.exit(3)

        if (ref_name != "v%s" % version):
            print("Error: Inconsistent tag name for release")
            sys.exit(4)

    else:
        # Check that we haven't pushed a release commit on master or a
        # maintenance branch. (Going further with the build would upload
        # artefacts that look like release artefacts on the snapshot
        # repository.)

        if ref_name == "master" or re.match("^maintenance_.*$", ref_name):
            if not "SNAPSHOT" in version:
                print("Error: Trying to build a release from branch " + ref_name)
                print("Hint: Create a release branch if you want to make a release")
                sys.exit(5)
