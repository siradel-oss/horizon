import re
import jinja2
import os
import sys
import io
import platform

from pathlib import Path

def last(value, sep):
    elements = value.split(sep)
    return elements[len(elements) - 1]

def snake_case(value):
    return "_".join(re.findall('[A-Z]+[^A-Z]*', value)).lower()

def snake_to_pascal(value):
    return "".join([v[0].upper() + v[1:] for v in value.split("_")])

def snake_to_camel(value):
    return camel_case("".join([v[0].upper() + v[1:] for v in value.split("_")]))

def camel_case(value):
    return value[0].lower() + value[1:]

def rejoin(value, old, new):
    return new.join(value.split(old))

def type_path(full_name):
    elements = full_name.split('.')
    path = []
    previous_path = ""
    first = True

    for element in elements:
        if first:
            first = False
            previous_path = element
            path.append({"text": element, "path": ""})
        else:
            previous_path += "." + element
            path.append({"text": element, "path": previous_path})

    return path

def without_first(value, sep):
    return sep.join(value.split(sep)[1:])

def to_short_type_name(value):
    if value in PB_PRIMITIVE_TYPES:
        return snake_to_pascal(value)
    else:
        if value.startswith("HrzProtocol.") or value.startswith("HrzJobsProtocol."):
            value = without_first(value, ".")
    return rejoin(value, ".", "_")

def to_cpp_enum_value_name(value, enum_full_name):
    enum_name = to_short_type_name(enum_full_name)
    if "_" in enum_name:
        return enum_name + "_" + value
    return value

def to_cpp_field_name(value):
    if value == "descriptor":
        return "descriptor_"
    return value

def to_cpp_qualified_name(value):
    if value in PB_PRIMITIVE_TYPES:
        return CPP_PRIMITIVE_TYPES[value]
    return "::" + rejoin(value, ".", "::")

def to_cpp_forward_declaration(value):
    type = "struct"
    if value.startswith("struct "):
        value = value[7:]
    elif value.startswith("class "):
        type = "class"
        value = value[6:]

    elements = value.split("::")
    return f'namespace {"::".join(elements[0:-1])} {{ {type} {elements[-1]}; }}'

def to_ts_type(value):
    if value in PB_PRIMITIVE_TYPES:
        return TS_PRIMITIVE_TYPES[value]
    return value

def to_ts_interface(value, enum_names):
    if value in PB_PRIMITIVE_TYPES:
        return TS_PRIMITIVE_TYPES[value]
    if value in enum_names:
        return value
    elements = value.split(".")
    elements[len(elements) - 1] = "I" + elements[len(elements) - 1]
    return ".".join(elements)

def to_cs_type(value):
    if value in PB_PRIMITIVE_TYPES:
        return CS_PRIMITIVE_TYPES[value]
    return value

def indent_prefix(text, indent = 0, prefix = ""):
    if text == "":
        return ""
    line_prefix = "\n" + " " * indent + prefix
    lines = text.split("\n")
    return line_prefix.join(lines)

def to_documentation_block(text, indent = 0, open_block = True, close_block = True):
    if text == "":
        return ""
    prefix = " " * indent + " *"
    start = "/**\n" + prefix + " " if open_block else ""
    end = "\n" + prefix + "/" if close_block else ""
    return start + indent_prefix(text, indent, " * ") + end

def to_wrapper(value):
    if value in PB_PRIMITIVE_TYPES:
        return PB_PRIMITIVE_WRAPPERS[value]
    return PB_PRIMITIVE_WRAPPERS["uint32"] # for enums

def prepare_env():
    current_path = Path(os.getcwd()) / "hrz/generators/templates"
    if platform.system() == "Linux":
        current_path = Path(os.path.dirname(__file__)) / "templates"

    loader = jinja2.FileSystemLoader(current_path)
    tpl_env = jinja2.Environment(loader = loader)
    tpl_env.filters["snake_case"] = snake_case
    tpl_env.filters["snake_to_pascal"] = snake_to_pascal
    tpl_env.filters["camel_case"] = camel_case
    tpl_env.filters["snake_to_camel"] = snake_to_camel
    tpl_env.filters["rejoin"] = rejoin
    tpl_env.filters["last"] = last
    tpl_env.filters["type_path"] = type_path
    tpl_env.filters["without_first"] = without_first
    tpl_env.filters["to_short_type_name"] = to_short_type_name
    tpl_env.filters["to_cpp_enum_value_name"] = to_cpp_enum_value_name
    tpl_env.filters["to_cpp_field_name"] = to_cpp_field_name
    tpl_env.filters["to_cpp_qualified_name"] = to_cpp_qualified_name
    tpl_env.filters["to_cpp_forward_declaration"] = to_cpp_forward_declaration
    tpl_env.filters["to_ts_type"] = to_ts_type
    tpl_env.filters["to_ts_interface"] = to_ts_interface
    tpl_env.filters["to_cs_type"] = to_cs_type
    tpl_env.filters["indent_prefix"] = indent_prefix
    tpl_env.filters["to_documentation_block"] = to_documentation_block
    tpl_env.filters["to_wrapper"] = to_wrapper
    tpl_env.trim_blocks = True
    tpl_env.lstrip_blocks = True
    return tpl_env

def remove_prefix(value: str, prefix: str) -> str:
    if value.startswith(prefix):
        return value[len(prefix):]
    return value

def output_template(value, tpl, output_path, name):
    output_path = Path(output_path)
    if not output_path.exists():
        output_path.mkdir(parents=True)
    output_file_path = output_path / name
    content = tpl.render(value)
    fp = io.open(output_file_path, mode="w+", encoding="utf8")
    fp.write(content)
    fp.close()

PB_PRIMITIVE_TYPES = [
    "double",
    "float",
    "int32",
    "int64",
    "sint32",
    "sint64",
    "uint32",
    "uint64",
    "fixed32",
    "fixed64",
    "sfixed32",
    "sfixed64",
    "bool",
    "string",
    "bytes",
]

PB_PRIMITIVE_WRAPPERS = {
    "double": "DoubleValue",
    "float": "FloatValue",
    "int32": "Int32Value",
    "int64": "Int64Value",
    "sint32": "Int32Value",
    "sint64": "Int64Value",
    "uint32": "UInt32Value",
    "uint64": "UInt64Value",
    "fixed32": "UInt32Value",
    "fixed64": "UInt64Value",
    "sfixed32": "Int32Value",
    "sfixed64": "Int64Value",
    "bool": "BoolValue",
    "string": "StringValue",
    "bytes": "BytesValue",
}

CPP_PRIMITIVE_TYPES = {
    "double": "double",
    "float": "float",
    "int32": "int32_t",
    "int64": "int64_t",
    "sint32": "int32_t",
    "sint64": "int64_t",
    "uint32": "uint32_t",
    "uint64": "uint64_t",
    "fixed32": "uint32_t",
    "fixed64": "uint64_t",
    "sfixed32": "int32_t",
    "sfixed64": "int64_t",
    "bool": "bool",
    "string": "std::string",
    "bytes": "std::string",
}

TS_PRIMITIVE_TYPES = {
    "double": "number",
    "float": "number",
    "int32": "number",
    "int64": "number|Long",
    "sint32": "number",
    "sint64": "number|Long",
    "uint32": "number",
    "uint64": "number|Long",
    "fixed32": "number",
    "fixed64": "number|Long",
    "sfixed32": "number",
    "sfixed64": "number|Long",
    "bool": "boolean",
    "string": "string",
    "bytes": "Uint8Array",
}

CS_PRIMITIVE_TYPES = {
    "double": "double",
    "float": "float",
    "int32": "int",
    "int64": "long",
    "sint32": "int",
    "sint64": "long",
    "uint32": "uint",
    "uint64": "ulong",
    "fixed32": "uint",
    "fixed64": "ulong",
    "sfixed32": "int",
    "sfixed64": "long",
    "bool": "bool",
    "string": "string",
    "bytes": "ByteString",
}

def gather_path_types(protocol, enum_names, message_type, gathered_path_types, to_forward_declare, visited_types = [], visit_stack = []):
    if message_type in visit_stack:
        # Circular dependency
        if not message_type in to_forward_declare:
            to_forward_declare.append(message_type)
        return

    if message_type in visited_types:
        # Prevent infinite recursion
        return
    visited_types.append(message_type)

    enum_type = next((e for e in protocol["enums"] if e["full_name"] == message_type), None)
    msg_type = next((m for m in protocol["messages"] if m["full_name"] == message_type), None)

    if message_type in PB_PRIMITIVE_TYPES:
        primitive_type = {
            "is_primitive": True,
            "is_enum": False,
            "type": message_type,
            "full_name": message_type,
            "file": "hrz_wrappers",
        }
        gathered_path_types.append(primitive_type)
    elif enum_type != None:
        enum_type["type"] = message_type
        enum_type["is_primitive"] = False
        enum_type["is_enum"] = True
        gathered_path_types.append(enum_type)
    elif msg_type != None:
        msg_type["is_primitive"] = False
        msg_type["is_enum"] = False
        if not msg_type["is_path_leaf"]:
            if any(f for f in msg_type["fields"] if f["union"] != None):
                print("Unions are not supported in paths (%s)" % message_type)
                sys.exit(1)

            for f in msg_type["fields"]:
                f["is_primitive"] = f["type"] in PB_PRIMITIVE_TYPES
                f["is_enum"] = f["type"] in enum_names
                gather_path_types(protocol, enum_names, f["type"], gathered_path_types, to_forward_declare, visited_types, visit_stack + [message_type])
        gathered_path_types.append(msg_type)

def find_by_full_name(full_name, collection):
    return next((item for item in collection if item["full_name"] == full_name))

def gather_client_messages(protocol):
    client_typed_message = find_by_full_name("HrzProtocol.TypedMessage", protocol["messages"])
    client_message_type_enum = find_by_full_name("HrzProtocol.MessageType", protocol["enums"])

    client_messages = []
    for enum_value in client_message_type_enum["values"]:
        for field in client_typed_message["fields"]:
            if field["name"] == enum_value["params_field_name"]:
                message = find_by_full_name(field["type"], protocol["messages"])
                client_messages.append({
                    "enum_value": enum_value,
                    "message": message,
                })
                break

    return client_messages

def prepare_api_tpl_data(protocol):
    root_types = {msg["full_name"]: msg["path_root"] for msg in protocol["messages"] if msg["is_path_root"]}
    path_types = []
    to_forward_declare = []

    enum_names = [e["full_name"] for e in protocol["enums"]]

    for root_type in root_types:
        gather_path_types(protocol, enum_names, root_type, path_types, to_forward_declare)

    return {
        "protocol": protocol,
        "path_types": path_types,
        "enum_names": enum_names,
        "client_messages": gather_client_messages(protocol),
        "to_forward_declare": to_forward_declare,
    }
