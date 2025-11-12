_PROTO_FILES = [
    "camera",
    "client_data",
    "input",
    "layers",
    "mapbox",
    "messages",
    "monitoring",
    "picking",
    "scene_dump",
    "scene_model",
    "shape_editor",
    "types",
    "viewer",
    "wrappers",
]

def list_proto_files(prefix, suffix):
    files = []
    for base_name in _PROTO_FILES:
        files.append(prefix + base_name + suffix)
    return files
