# SPDX-FileCopyrightText: Copyright 2026 Siradel
# SPDX-License-Identifier: MIT

from hrz.generator.proto_types import *
from collections.abc import Callable
import re

FILTERS: dict[str, Callable] = {}


def register_filter(func):
    FILTERS[func.__name__] = func
    return func


@register_filter
def last(value, sep):
    elements = value.split(sep)
    return elements[len(elements) - 1]


@register_filter
def path_to_snake_case(value: str):
    return value.replace("/", "_").replace("\\", "_").replace(".", "_")


@register_filter
def snake_case(value):
    return "_".join(re.findall("[A-Z]+[^A-Z]*", value)).lower()


@register_filter
def snake_to_pascal(value):
    return "".join([v[0].upper() + v[1:] for v in value.split("_")])


@register_filter
def snake_to_camel(value):
    return camel_case("".join([v[0].upper() + v[1:] for v in value.split("_")]))


@register_filter
def camel_case(value):
    return value[0].lower() + value[1:]


@register_filter
def rejoin(value, old, new):
    return new.join(value.split(old))


@register_filter
def wbr_on_slash(value: str) -> str:
    return value.replace("/", "/<wbr />").replace("\\", "\\<wbr />")


@register_filter
def wbr_on_period(value: str) -> str:
    return value.replace(".", ".<wbr />")


@register_filter
def type_path(full_name):
    elements = full_name.split(".")
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


@register_filter
def without_first(value, sep):
    return sep.join(value.split(sep)[1:])


@register_filter
def to_short_type_name(value):
    if value in PB_PRIMITIVE_TYPES:
        return snake_to_pascal(value)
    else:
        if value.startswith("HrzProtocol.") or value.startswith("HrzJobsProtocol."):
            value = without_first(value, ".")
    return rejoin(value, ".", "_")


@register_filter
def to_cpp_enum_value_name(value, enum_full_name):
    enum_name = to_short_type_name(enum_full_name)
    if "_" in enum_name:
        return enum_name + "_" + value
    return value


@register_filter
def to_cpp_field_name(value):
    if value == "descriptor":
        return "descriptor_"
    return value


@register_filter
def to_cpp_qualified_name(value):
    if value in PB_PRIMITIVE_TYPES:
        return CPP_PRIMITIVE_TYPES[value]
    return "::" + rejoin(value, ".", "::")


@register_filter
def to_cpp_forward_declaration(value):
    type = "struct"
    if value.startswith("struct "):
        value = value[7:]
    elif value.startswith("class "):
        type = "class"
        value = value[6:]

    elements = value.split("::")
    return f'namespace {"::".join(elements[0:-1])} {{ {type} {elements[-1]}; }}'


@register_filter
def to_ts_type(value, enum_names):
    if value in PB_PRIMITIVE_TYPES:
        return TS_PRIMITIVE_TYPES[value]
    elif value in enum_names:
        return value
    else:
        return f"{value} & {value}.$Shape"


@register_filter
def to_ts_interface(value, enum_names):
    if value in PB_PRIMITIVE_TYPES:
        return TS_PRIMITIVE_TYPES[value]
    if value in enum_names:
        return value
    elements = value.split(".")
    elements[len(elements) - 1] = elements[len(elements) - 1] + ".$Properties"
    return ".".join(elements)


@register_filter
def indent_prefix(text, indent=0, prefix=""):
    if text == "":
        return ""
    line_prefix = "\n" + " " * indent + prefix
    lines = text.split("\n")
    return line_prefix.join(lines)


@register_filter
def to_documentation_block(text, indent=0, open_block=True, close_block=True):
    text = text.strip()
    if text == "":
        return ""
    prefix = " " * indent + " *"
    start = "/**\n" + prefix + " " if open_block else ""
    end = "\n" + prefix + "/" if close_block else ""
    return start + indent_prefix(text, indent, " * ") + end


@register_filter
def to_wrapper(value):
    if value in PB_PRIMITIVE_TYPES:
        return PB_PRIMITIVE_WRAPPERS[value]
    return PB_PRIMITIVE_WRAPPERS["uint32"]  # for enums
