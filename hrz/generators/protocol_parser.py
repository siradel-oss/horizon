import xml.etree.ElementTree as ElementTree
import re

def remove_package(full_name, package):
    if full_name.startswith(package + "."):
        return full_name[len(package + "."):]
    return full_name

def remove_leading_spaces(text: str):
    lines = text.splitlines()
    if len(lines) == 0:
        return text

    non_empty_lines = [line for line in lines if line.strip()]
    if not non_empty_lines:
        return text

    min_leading_spaces = min(len(line) - len(line.lstrip(" ")) for line in non_empty_lines)
    return "\n".join(line[min_leading_spaces:] for line in lines)

def parse(input_file_name):
    content = open(input_file_name, "r", encoding="utf-8").read()
    root = ElementTree.fromstring(content)

    protocol = {}
    protocol["services"] = []
    protocol["enums"] = []
    protocol["messages"] = []
    protocol["files"] = []

    path_root_message = root.find("file/message[full_name='HrzProtocol.PathRoot']")

    for proto_file in root.iter("file"):
        file_basename = proto_file.find("filename").text[:-6]
        dependencies = []

        for dep in proto_file.iter("dependency"):
            dependencies.append(dep.text[:-6])

        protocol["files"].append({
            "name": file_basename,
            "dependencies": dependencies,
        })

        package = proto_file.find("package").text

        for s in proto_file.iter("service"):
            service = {}
            service["file"] = file_basename
            service["full_name"] = s.find("full_name").text
            service["name"] = remove_package(service["full_name"], package)
            service["package"] = package
            service["documentation"] = remove_leading_spaces(s.findtext("documentation", default=""))
            service["id"] = int(s.find("id").text)
            service["methods"] = []

            for m in s.iter("method"):
                method = {}
                method["name"] = m.find("name").text
                method["input"] = m.find("input").text
                method["output"] = m.find("output").text
                method["documentation"] = remove_leading_spaces(m.findtext("documentation", default=""))
                method["deprecated"] = True if m.find("deprecated").text == "true" else False
                method["id"] = int(m.find("id").text)
                service["methods"].append(method)

            protocol["services"].append(service)

        for e in proto_file.iter("enum"):
            enum = {}
            enum["file"] = file_basename
            enum["full_name"] = e.find("full_name").text
            enum["name"] = remove_package(enum["full_name"], package)
            enum["package"] = package
            enum["documentation"] = remove_leading_spaces(e.findtext("documentation", default=""))
            enum["expose_to_style"] = True if e.find("expose_to_style").text == "true" else False
            enum["values"] = []

            for v in e.iter("value"):
                val = {}
                val["name"] = v.find("name").text
                val["id"] = int(v.find("id").text)
                val["deprecated"] = True if v.find("deprecated").text == "true" else False
                val["documentation"] = remove_leading_spaces(v.find("documentation").text)
                val["params_field_name"] = v.findtext("params_field_name", default="")
                val["response_field_name"] = v.findtext("response_field_name", default="")
                val["params_type"] = v.findtext("params_type", default="")
                val["response_type"] = v.findtext("response_type", default="")
                val["label"] = v.findtext("label", default=val["name"])
                enum["values"].append(val)

            protocol["enums"].append(enum)

        for m in proto_file.iter("message"):
            msg = {}
            msg["file"] = file_basename
            msg["full_name"] = m.find("full_name").text
            msg["name"] = remove_package(msg["full_name"], package)
            msg["package"] = package
            msg["documentation"] = remove_leading_spaces(m.findtext("documentation", default=""))
            msg["fields"] = []

            path_root_node = m.find("path_root")
            if path_root_node != None:
                root_field = path_root_message.find("field[union='kind'][name='%s']" % path_root_node.text)
                msg["path_root"] = path_root_node.text
                msg["path_root_type"] = root_field.find("type").text
                root_type_is_enum = root.find("file/enum[full_name='%s']" % msg["path_root_type"]) != None
                msg["path_root_type_is_enum"] = root_type_is_enum
                msg["is_path_root"] = True
            else:
                msg["is_path_root"] = False

            msg["is_path_leaf"] = True if m.find("path_leaf").text == "true" else False

            for f in m.iter("field"):
                field = {}
                field["name"] = f.find("name").text
                field["id"] = int(f.find("id").text)
                field["union"] = f.find("union").text
                field["type"] = f.find("type").text
                field["repeated"] = True if f.find("repeated").text == "true" else False
                field["deprecated"] = True if f.find("deprecated").text == "true" else False
                field["optional"] = True if f.find("optional").text == "true" else False
                field["documentation"] = remove_leading_spaces(f.findtext("documentation", default=""))
                msg["fields"].append(field)

            protocol["messages"].append(msg)

    return protocol

