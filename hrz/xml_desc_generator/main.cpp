#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>

#include <cctype>
#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// This is used to detect id collisions.
// First index is service id.
// The set contains method id.
//
// This doesn't use hashes because in case of collision
// we'd have to change the seed, and thus break
// protocol compatibility.
std::set<uint64_t> service_ids;

std::string parse_attribute(std::string& source, const char* attribute_name)
{
    int attribute_len = strlen(attribute_name);
    std::stringstream ss(source);
    std::string remaining_comment;
    std::string attribute_line;
    std::string attribute_value;

    while (std::getline(ss, attribute_line, '\n'))
    {
        bool is_comment = true;

        if (auto at_location = attribute_line.find('@'); at_location != std::string::npos)
        {
            is_comment = false;

            // Check that everything before is whitespace.
            for (decltype(at_location) it = 0; it < at_location; ++it)
            {
                if (!std::isspace(attribute_line[it]))
                {
                    is_comment = true;
                    break;
                }
            }

            if (strncmp(attribute_name, attribute_line.c_str() + at_location + 1, attribute_len)
                != 0)
            {
                is_comment = true;
            }
            else if (!is_comment)
            {
                // Find the '=', and then start looking for the value after it.
                const char* value_find_start =
                    attribute_line.c_str() + at_location + 1 + attribute_len;
                const char* value_find_end = attribute_line.c_str() + attribute_line.size();
                auto eq_pos = std::find(value_find_start, value_find_end, '=');
                if (eq_pos == value_find_end)
                {
                    is_comment = true;
                }
                else
                {
                    value_find_start = eq_pos + 1;

                    // Trim the value
                    while (std::isspace(*value_find_start) && value_find_start != value_find_end)
                    {
                        value_find_start += 1;
                    }

                    while (std::isspace(*value_find_end) && value_find_end != value_find_start)
                    {
                        value_find_end -= 1;
                    }

                    attribute_value = std::string(value_find_start, value_find_end);
                }
            }
        }

        if (is_comment)
        {
            remaining_comment += attribute_line;
            remaining_comment += "\n";
        }
    }

    source.swap(remaining_comment);
    return attribute_value;
}

bool parse_attribute_hex(std::string& source, const char* attribute_name, uint64_t* result)
{
    std::string attribute_value = parse_attribute(source, attribute_name);
    if (attribute_value.size() < 3)
    {
        return false;
    }

    if (attribute_value[0] != '0' || attribute_value[1] != 'x')
    {
        return false;
    }

    *result = 0;

    for (size_t i = 2; i < attribute_value.size(); ++i)
    {
        char c = std::tolower(attribute_value[i]);
        int value = 0;
        if (c >= '0' && c <= '9')
            value = (int)c - '0';
        else if (c >= 'a' && c <= 'f')
            value = (int)c - 'a' + 10;
        else
            return false;

        *result = *result * 16 + value;
    }

    return true;
}

bool parse_attribute_string(std::string& source, const char* attribute_name, std::string* result)
{
    std::string attribute_value = parse_attribute(source, attribute_name);
    if (attribute_value.size() < 2)
    {
        return false;
    }

    if (attribute_value.front() != '"' || attribute_value.back() != '"')
    {
        return false;
    }

    *result = std::string(attribute_value.begin() + 1, attribute_value.end() - 1);
    return true;
}

void print_service(
    google::protobuf::io::Printer& printer,
    const google::protobuf::ServiceDescriptor* s)
{
    printer.Print("    <service>\n");
    printer.Print("        <full_name>$name$</full_name>\n", "name", s->full_name());

    std::set<uint64_t> method_ids;

    std::string documentation;
    if (google::protobuf::SourceLocation loc; s->GetSourceLocation(&loc))
    {
        documentation += loc.leading_comments;
        documentation += " ";
        documentation += loc.trailing_comments;
    }

    uint64_t service_id = 0;
    if (!parse_attribute_hex(documentation, "service_id", &service_id))
    {
        std::cerr << "No service ID found for " << s->full_name() << std::endl;
        exit(1);
    }

    if (service_ids.contains(service_id))
    {
        std::cerr << "Service ID duplicated for " << s->full_name() << std::endl;
        exit(1);
    }
    service_ids.insert(service_id);

    printer.Print("        <id>$id$</id>\n", "id", std::to_string(service_id));

    for (int i = 0; i < s->method_count(); ++i)
    {
        auto method = s->method(i);
        printer.Print("        <method>\n");
        printer.Print("            <name>$name$</name>\n", "name", method->name());
        printer.Print(
            "            <input>$type$</input>\n", "type", method->input_type()->full_name());
        printer.Print(
            "            <output>$type$</output>\n", "type", method->output_type()->full_name());

        std::string method_doc;
        google::protobuf::SourceLocation loc;
        if (method->GetSourceLocation(&loc))
        {
            method_doc += loc.leading_comments;
            method_doc += " ";
            method_doc += loc.trailing_comments;
        }

        uint64_t method_id = 0;
        if (!parse_attribute_hex(method_doc, "method_id", &method_id))
        {
            std::cerr << "No method ID for " << method->full_name() << std::endl;
            exit(1);
        }

        if (method_ids.contains(method_id))
        {
            std::cerr << "Method ID duplicated for " << method->full_name() << std::endl;
            exit(1);
        }

        method_ids.insert(method_id);
        printer.Print("            <id>$id$</id>\n", "id", std::to_string(method_id));

        if (method->options().has_deprecated() && method->options().deprecated())
        {
            printer.Print("            <deprecated>true</deprecated>\n");
        }
        else
        {
            printer.Print("            <deprecated>false</deprecated>\n");
        }

        std::string documentation = "";
        if (method->GetSourceLocation(&loc))
        {
            documentation += loc.leading_comments;
            documentation += " ";
            documentation += loc.trailing_comments;
        }
        printer.Print(
            "            <documentation><![CDATA[$doc$]]></documentation>\n", "doc", method_doc);
        printer.Print("        </method>\n");
    }

    printer.Print(
        "        <documentation><![CDATA[$doc$]]></documentation>\n", "doc", documentation);
    printer.Print("    </service>\n");
}

void print_enum(google::protobuf::io::Printer& printer, const google::protobuf::EnumDescriptor* e)
{
    printer.Print("    <enum>\n");
    printer.Print("        <full_name>$name$</full_name>\n", "name", e->full_name());

    std::string documentation;
    google::protobuf::SourceLocation loc;
    if (e->GetSourceLocation(&loc))
    {
        documentation += loc.leading_comments;
        documentation += " ";
        documentation += loc.trailing_comments;
    }

    if (std::string expose_to_style;
        parse_attribute_string(documentation, "expose_to_style", &expose_to_style)
        && expose_to_style == "true")
    {
        printer.Print("        <expose_to_style>true</expose_to_style>\n");
    }
    else
    {
        printer.Print("        <expose_to_style>false</expose_to_style>\n");
    }

    for (int i = 0; i < e->value_count(); ++i)
    {
        auto value = e->value(i);

        std::string value_doc = "";
        if (value->GetSourceLocation(&loc))
        {
            value_doc += loc.leading_comments;
            value_doc += " ";
            value_doc += loc.trailing_comments;
        }

        printer.Print("        <value>\n");
        printer.Print("            <name>$name$</name>\n", "name", value->name());
        printer.Print("            <id>$id$</id>\n", "id", std::to_string(value->number()));

        if (value->options().has_deprecated() && value->options().deprecated())
        {
            printer.Print("            <deprecated>true</deprecated>\n");
        }
        else
        {
            printer.Print("            <deprecated>false</deprecated>\n");
        }

        std::string attribute;
        if (parse_attribute_string(value_doc, "label", &attribute))
        {
            printer.Print("            <label>$name$</label>\n", "name", attribute);
        }

        if (parse_attribute_string(value_doc, "params_field_name", &attribute))
        {
            printer.Print(
                "            <params_field_name>$name$</params_field_name>\n", "name", attribute);
        }

        if (parse_attribute_string(value_doc, "response_field_name", &attribute))
        {
            printer.Print(
                "            <response_field_name>$name$</response_field_name>\n", "name",
                attribute);
        }

        printer.Print(
            "            <documentation><![CDATA[$doc$]]></documentation>\n", "doc", value_doc);
        printer.Print("        </value>\n");
    }

    printer.Print(
        "        <documentation><![CDATA[$doc$]]></documentation>\n", "doc", documentation);
    printer.Print("    </enum>\n");
}

void print_message(google::protobuf::io::Printer& printer, const google::protobuf::Descriptor* msg)
{
    printer.Print("    <message>\n");
    printer.Print("        <full_name>$name$</full_name>\n", "name", msg->full_name());

    std::string documentation;
    google::protobuf::SourceLocation loc;
    if (msg->GetSourceLocation(&loc))
    {
        documentation += loc.leading_comments;
        documentation += " ";
        documentation += loc.trailing_comments;
    }

    if (std::string path_root; parse_attribute_string(documentation, "path_root", &path_root))
    {
        printer.Print("        <path_root>$root$</path_root>\n", "root", path_root);
    }

    if (std::string is_path_leaf;
        parse_attribute_string(documentation, "path_leaf", &is_path_leaf) && is_path_leaf == "true")
    {
        printer.Print("        <path_leaf>true</path_leaf>\n");
    }
    else
    {
        printer.Print("        <path_leaf>false</path_leaf>\n");
    }

    for (int i = 0; i < msg->field_count(); ++i)
    {
        auto value = msg->field(i);

        printer.Print("        <field>\n");
        printer.Print("            <name>$name$</name>\n", "name", value->name());
        printer.Print("            <id>$id$</id>\n", "id", std::to_string(value->number()));

        // Optional for primitive types is implemented as a oneof with only one field.
        if (value->containing_oneof() && !value->real_containing_oneof())
        {
            printer.Print("            <optional>true</optional>\n");
        }
        else
        {
            printer.Print("            <optional>false</optional>\n");
        }

        if (value->real_containing_oneof())
        {
            printer.Print(
                "            <union>$union$</union>\n", "union", value->containing_oneof()->name());
        }
        else
        {
            printer.Print("            <union></union>\n");
        }

        if (value->is_repeated())
        {
            printer.Print("            <repeated>true</repeated>\n");
        }
        else
        {
            printer.Print("            <repeated>false</repeated>\n");
        }

        if (value->type() == google::protobuf::FieldDescriptor::Type::TYPE_MESSAGE)
        {
            printer.Print(
                "            <type>$type$</type>\n", "type", value->message_type()->full_name());
        }
        else if (value->type() == google::protobuf::FieldDescriptor::Type::TYPE_ENUM)
        {
            printer.Print(
                "            <type>$type$</type>\n", "type", value->enum_type()->full_name());
        }
        else
        {
            printer.Print(
                "            <type>$type$</type>\n", "type", value->TypeName(value->type()));
        }

        if (value->options().has_deprecated() && value->options().deprecated())
        {
            printer.Print("            <deprecated>true</deprecated>\n");
        }
        else
        {
            printer.Print("            <deprecated>false</deprecated>\n");
        }

        std::string documentation = "";
        if (value->GetSourceLocation(&loc))
        {
            documentation += loc.leading_comments;
            documentation += " ";
            documentation += loc.trailing_comments;
        }
        printer.Print(
            "            <documentation><![CDATA[$doc$]]></documentation>\n", "doc", documentation);

        printer.Print("        </field>\n");
    }

    printer.Print(
        "        <documentation><![CDATA[$doc$]]></documentation>\n", "doc", documentation);
    printer.Print("    </message>\n");

    for (int i = 0; i < msg->nested_type_count(); ++i)
    {
        print_message(printer, msg->nested_type(i));
    }

    for (int i = 0; i < msg->enum_type_count(); ++i)
    {
        print_enum(printer, msg->enum_type(i));
    }
}

class Generator : public google::protobuf::compiler::CodeGenerator
{
    uint64_t GetSupportedFeatures() const override
    {
        return CodeGenerator::Feature::FEATURE_PROTO3_OPTIONAL;
    }

    bool Generate(
        const google::protobuf::FileDescriptor* file,
        const std::string& parameter,
        google::protobuf::compiler::GeneratorContext* generator_context,
        std::string* error) const override
    {
        return true;
    }

    bool GenerateAll(
        const std::vector<const google::protobuf::FileDescriptor*>& files,
        const std::string& parameter,
        google::protobuf::compiler::GeneratorContext* generator_context,
        std::string* error) const override
    {
        auto stream = generator_context->Open("hrz_protocol.xml");
        google::protobuf::io::Printer printer(stream, '$');

        printer.Print("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>\n");
        printer.Print("<protocol>\n");

        for (const auto file : files)
        {
            printer.Print("    <file>\n");
            printer.Print("        <filename>$filename$</filename>\n", "filename", file->name());
            printer.Print("        <package>$package$</package>\n", "package", file->package());

            for (int i = 0; i < file->dependency_count(); ++i)
            {
                printer.Print(
                    "        <dependency>$dependency$</dependency>\n", "dependency",
                    file->dependency(i)->name());
            }

            for (int i = 0; i < file->service_count(); ++i)
            {
                print_service(printer, file->service(i));
            }

            for (int i = 0; i < file->enum_type_count(); ++i)
            {
                print_enum(printer, file->enum_type(i));
            }

            for (int i = 0; i < file->message_type_count(); ++i)
            {
                print_message(printer, file->message_type(i));
            }

            printer.Print("    </file>\n");
        }

        printer.Print("</protocol>\n");
        return true;
    }
};

int main(int argc, char* argv[])
{
    Generator generator;
    google::protobuf::compiler::PluginMain(argc, argv, &generator);
    return 0;
}
