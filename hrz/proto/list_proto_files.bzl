_PROTO_FILES = [
    "hrz_wrappers",
    "hrz_types",
    "hrz_layers",
    "hrz_viewer",
    "hrz_scene_model",
    "hrz_messages",
    "hrz_client_data",
    "hrz_picking",
    "hrz_camera",
    "hrz_shape_editor",
    "hrz_input",
    "hrz_scene_dump",
    "hrz_monitoring",
    "hrz_mapbox",
]
def list_proto_files(prefix, suffix):
    files = []
    for base_name in _PROTO_FILES:
        files.append(prefix + base_name + suffix)
    return files

def list_proto_files_pascal_case(prefix, suffix):
    files = []
    for base_name in _PROTO_FILES:
        new_name = "".join([f[0:1].upper() + f[1:] for f in base_name.split("_")])
        files.append(prefix + new_name + suffix)

    return files
