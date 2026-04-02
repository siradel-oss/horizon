import xml.etree.ElementTree as ElementTree
from .protocol_model import (
    HrzProtocol,
    HrzService,
    HrzMethod,
    HrzEnum,
    HrzEnumValue,
    HrzMessage,
    HrzField,
    HrzFile,
)


def remove_package(full_name: str, package: str) -> str:
    if full_name.startswith(package + "."):
        return full_name[len(package + ".") :]
    return full_name


def remove_common_leading_spaces(text: str) -> str:
    lines = text.splitlines()
    if len(lines) == 0:
        return text

    non_empty_lines = [line for line in lines if line.strip()]
    if not non_empty_lines:
        return text

    min_leading_spaces = min(
        len(line) - len(line.lstrip(" ")) for line in non_empty_lines
    )
    return "\n".join(line[min_leading_spaces:] for line in lines)


def find_text_or_none(parent: ElementTree.Element, path: str) -> str | None:
    node = parent.find(path)
    return node.text if node is not None else None


def find_text(parent: ElementTree.Element, path: str) -> str:
    return find_text_or_none(parent, path) or ""


def parse(input_file_name) -> HrzProtocol:
    content = open(input_file_name, "r", encoding="utf-8").read()
    root = ElementTree.fromstring(content)

    services = []
    enums = []
    messages = []
    files = []

    path_root_message = root.find("file/message[full_name='HrzProtocol.PathRoot']")
    if path_root_message is None:
        raise Exception(
            "HrzProtocol.PathRoot message not found in protocol description"
        )

    for proto_file in root.iter("file"):
        file_basename = find_text(proto_file, "filename")[:-6]
        dependencies = []

        for dep in proto_file.iter("dependency"):
            dependencies.append(find_text(dep, ".")[:-6])

        files.append(HrzFile(name=file_basename, dependencies=dependencies))

        package = find_text(proto_file, "package")

        for s in proto_file.iter("service"):
            methods = []
            for m in s.iter("method"):
                method = HrzMethod(
                    name=find_text(m, "name"),
                    input=find_text(m, "input"),
                    output=find_text(m, "output"),
                    documentation=remove_common_leading_spaces(
                        find_text(m, "documentation")
                    ),
                    deprecated=find_text(m, "deprecated") == "true",
                    id=int(find_text(m, "id")),
                )
                methods.append(method)

            service = HrzService(
                file=file_basename,
                full_name=find_text(s, "full_name"),
                name=remove_package(find_text(s, "full_name"), package),
                package=package,
                documentation=remove_common_leading_spaces(
                    find_text(s, "documentation")
                ),
                id=int(find_text(s, "id")),
                methods=methods,
            )
            services.append(service)

        for e in proto_file.iter("enum"):
            values = []
            for v in e.iter("value"):
                val = HrzEnumValue(
                    name=find_text(v, "name"),
                    id=int(find_text(v, "id")),
                    deprecated=find_text(v, "deprecated") == "true",
                    documentation=remove_common_leading_spaces(
                        find_text(v, "documentation")
                    ),
                    params_field_name=find_text_or_none(v, "params_field_name"),
                    response_field_name=find_text_or_none(v, "response_field_name"),
                    label=find_text_or_none(v, "label") or find_text(v, "name"),
                )
                values.append(val)

            enum = HrzEnum(
                file=file_basename,
                full_name=find_text(e, "full_name"),
                name=remove_package(find_text(e, "full_name"), package),
                package=package,
                documentation=remove_common_leading_spaces(
                    find_text(e, "documentation")
                ),
                expose_to_style=find_text(e, "expose_to_style") == "true",
                values=values,
            )
            enums.append(enum)

        for m in proto_file.iter("message"):
            fields = []
            for f in m.iter("field"):
                field = HrzField(
                    name=find_text(f, "name"),
                    id=int(find_text(f, "id")),
                    union=find_text_or_none(f, "union"),
                    type=find_text(f, "type"),
                    repeated=find_text(f, "repeated") == "true",
                    deprecated=find_text(f, "deprecated") == "true",
                    optional=find_text(f, "optional") == "true",
                    documentation=remove_common_leading_spaces(
                        find_text(f, "documentation")
                    ),
                )
                fields.append(field)

            # Handle path root attributes
            path_root_node = m.find("path_root")
            path_root = None
            path_root_type = None
            path_root_type_is_enum = None
            is_path_root = False

            if path_root_node is not None:
                path_root = path_root_node.text
                root_field = path_root_message.find(
                    "field[union='kind'][name='%s']" % path_root
                )
                if root_field is None:
                    raise Exception(
                        "Failed to find root field for path root '%s'" % path_root
                    )
                path_root_type = find_text(root_field, "type")
                path_root_type_is_enum = (
                    root.find("file/enum[full_name='%s']" % path_root_type) is not None
                )
                is_path_root = True

            msg = HrzMessage(
                file=file_basename,
                full_name=find_text(m, "full_name"),
                name=remove_package(find_text(m, "full_name"), package),
                package=package,
                documentation=remove_common_leading_spaces(
                    find_text(m, "documentation")
                ),
                fields=fields,
                is_path_root=is_path_root,
                is_path_leaf=find_text(m, "path_leaf") == "true",
                path_root=path_root,
                path_root_type=path_root_type,
                path_root_type_is_enum=path_root_type_is_enum,
            )
            messages.append(msg)

    return HrzProtocol(services=services, enums=enums, messages=messages, files=files)
