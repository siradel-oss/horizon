load("@proto_files_list//:list.bzl", "PROTO_FILES")

def list_proto_files(prefix, suffix):
    files = []
    for base_name in PROTO_FILES:
        files.append(prefix + base_name + suffix)
    return files
