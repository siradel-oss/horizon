load("@rules_pkg//pkg:mappings.bzl", "pkg_files", "strip_prefix")
load("//tools/bazel:pkg_copy.bzl", "pkg_copy")

def structured_files_copy(name, files, output_dir):
    """Create a directory and its subfolders with the given files.

    This is a wrapper around pkg_copy and pkg_files when strip_prefix strategy
    is always files_only().
    This allows not having to write a bunch of pkg_files but is otherwise
    functionally equivalent.

    files has this structure:
    {
        "subfolder1": ["label1", "label2, ...],
        "subfolder2/another_one": ["label1", "label2, ...],
        ...
    }

    Args:
        name: The name of the target.
        files: A dict mapping folder names to lists of labels. The files from
            each label will be copied to the corresponding folder in the output
            directory.
        output_dir: The directory where the files will be copied to.
    """

    pkg_files_list = []

    for folder, labels in files.items():
        folder = folder.strip("/")
        folder_name = folder.replace("/", "_") if folder else "root"
        pkg_files_name = "{}_{}_files".format(name, folder_name)
        pkg_files_list.append(":{}".format(pkg_files_name))

        pkg_files(
            name = pkg_files_name,
            srcs = labels,
            prefix = folder,
            strip_prefix = strip_prefix.files_only(),
        )

    pkg_copy(
        name = name,
        srcs = pkg_files_list,
        out_dir = output_dir,
    )
