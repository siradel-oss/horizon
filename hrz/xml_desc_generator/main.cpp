// SPDX-FileCopyrightText: Copyright 2018 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/fnd/hash.h"

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

namespace
{

// This is used to detect id collisions.
// First index is service id.
// The set contains method id.
//
// This doesn't use hashes because in case of collision
// we'd have to change the seed, and thus break
// protocol compatibility.
std::set<uint32_t> service_ids;

template<typename T>
concept has_source_location = requires(const T* t, google::protobuf::SourceLocation* loc) {
    { t->GetSourceLocation(loc) } -> std::same_as<bool>;
};

template<has_source_location T>
std::string get_documentation(const T* descriptor)
{
    if (google::protobuf::SourceLocation loc; descriptor->GetSourceLocation(&loc))
    {
        std::string documentation;
        documentation += loc.leading_comments;
        documentation += "\n";
        documentation += loc.trailing_comments;
        return documentation;
    }
    else
    {
        return "";
    }
}

std::string parse_attribute(std::string& source, std::string_view attribute_name)
{
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

            if (strncmp(
                    attribute_name.data(), attribute_line.c_str() + at_location + 1,
                    attribute_name.size())
                != 0)
            {
                is_comment = true;
            }
            else if (!is_comment)
            {
                // Find the '=', and then start looking for the value after it.
                const char* value_find_start =
                    attribute_line.c_str() + at_location + 1 + attribute_name.size();
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

bool parse_attribute_string(
    std::string& source,
    std::string_view attribute_name,
    std::string* result)
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

    std::set<uint32_t> method_ids;

    const auto service_documentation = get_documentation(s);

    const auto service_id = static_cast<uint32_t>(hrz::murmur3_x64_64(s->full_name()));
    if (service_ids.contains(service_id))
    {
        std::cerr << "Service ID duplicated for " << s->full_name() << "\n";
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

        const auto method_id = static_cast<uint32_t>(hrz::murmur3_x64_64(method->full_name()));
        if (method_ids.contains(method_id))
        {
            std::cerr << "Method ID duplicated for " << method->full_name() << "\n";
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

        const auto method_documentation = get_documentation(method);
        printer.Print(
            "            <documentation><![CDATA[$doc$]]></documentation>\n", "doc",
            method_documentation);
        printer.Print("        </method>\n");
    }

    printer.Print(
        "        <documentation><![CDATA[$doc$]]></documentation>\n", "doc", service_documentation);
    printer.Print("    </service>\n");
}

void print_enum(google::protobuf::io::Printer& printer, const google::protobuf::EnumDescriptor* e)
{
    printer.Print("    <enum>\n");
    printer.Print("        <full_name>$name$</full_name>\n", "name", e->full_name());

    auto enum_documentation = get_documentation(e);

    if (std::string expose_to_style;
        parse_attribute_string(enum_documentation, "expose_to_style", &expose_to_style)
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

        auto value_documentation = get_documentation(value);

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
        if (parse_attribute_string(value_documentation, "label", &attribute))
        {
            printer.Print("            <label>$name$</label>\n", "name", attribute);
        }

        if (parse_attribute_string(value_documentation, "params_field_name", &attribute))
        {
            printer.Print(
                "            <params_field_name>$name$</params_field_name>\n", "name", attribute);
        }

        if (parse_attribute_string(value_documentation, "response_field_name", &attribute))
        {
            printer.Print(
                "            <response_field_name>$name$</response_field_name>\n", "name",
                attribute);
        }

        printer.Print(
            "            <documentation><![CDATA[$doc$]]></documentation>\n", "doc",
            value_documentation);
        printer.Print("        </value>\n");
    }

    printer.Print(
        "        <documentation><![CDATA[$doc$]]></documentation>\n", "doc", enum_documentation);
    printer.Print("    </enum>\n");
}

void print_message(google::protobuf::io::Printer& printer, const google::protobuf::Descriptor* msg)
{
    printer.Print("    <message>\n");
    printer.Print("        <full_name>$name$</full_name>\n", "name", msg->full_name());

    auto message_documentation = get_documentation(msg);

    if (std::string path_root;
        parse_attribute_string(message_documentation, "path_root", &path_root))
    {
        printer.Print("        <path_root>$root$</path_root>\n", "root", path_root);
    }

    if (std::string is_path_leaf;
        parse_attribute_string(message_documentation, "path_leaf", &is_path_leaf)
        && is_path_leaf == "true")
    {
        printer.Print("        <path_leaf>true</path_leaf>\n");
    }
    else
    {
        printer.Print("        <path_leaf>false</path_leaf>\n");
    }

    for (int i = 0; i < msg->real_oneof_decl_count(); ++i)
    {
        auto oneof = msg->real_oneof_decl(i);
        printer.Print("        <union>\n");
        printer.Print("            <name>$name$</name>\n", "name", oneof->name());

        auto oneof_documentation = get_documentation(oneof);
        printer.Print(
            "            <documentation><![CDATA[$doc$]]></documentation>\n", "doc",
            oneof_documentation);

        for (int j = 0; j < oneof->field_count(); ++j)
        {
            auto field = oneof->field(j);
            printer.Print("            <field>\n");
            printer.Print("                <id>$id$</id>\n", "id", std::to_string(field->number()));
            printer.Print("                <name>$name$</name>\n", "name", field->name());
            printer.Print("            </field>\n");
        }
        printer.Print("        </union>\n");
    }

    for (int i = 0; i < msg->field_count(); ++i)
    {
        auto value = msg->field(i);

        printer.Print("        <field>\n");
        printer.Print("            <name>$name$</name>\n", "name", value->name());
        printer.Print("            <id>$id$</id>\n", "id", std::to_string(value->number()));

        if (value->has_presence() && !value->real_containing_oneof()
            && value->type() != google::protobuf::FieldDescriptor::Type::TYPE_MESSAGE)
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

        const auto field_documentation = get_documentation(value);
        printer.Print(
            "            <documentation><![CDATA[$doc$]]></documentation>\n", "doc",
            field_documentation);

        printer.Print("        </field>\n");
    }

    printer.Print(
        "        <documentation><![CDATA[$doc$]]></documentation>\n", "doc", message_documentation);
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
        return CodeGenerator::Feature::FEATURE_PROTO3_OPTIONAL
            | CodeGenerator::FEATURE_SUPPORTS_EDITIONS;
    }

    google::protobuf::Edition GetMinimumEdition() const override
    {
        return google::protobuf::Edition::EDITION_PROTO3;
    }

    google::protobuf::Edition GetMaximumEdition() const override
    {
        return google::protobuf::Edition::EDITION_2026;
    }

    bool Generate(
        const google::protobuf::FileDescriptor*,
        const std::string& /* parameter */,
        google::protobuf::compiler::GeneratorContext*,
        std::string* /* error */) const override
    {
        return true;
    }

    bool GenerateAll(
        const std::vector<const google::protobuf::FileDescriptor*>& files,
        const std::string& /* parameter */,
        google::protobuf::compiler::GeneratorContext* generator_context,
        std::string* /* error */) const override
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

} // anonymous namespace

int main(int argc, char* argv[])
{
    Generator generator;
    google::protobuf::compiler::PluginMain(argc, argv, &generator);
    return 0;
}
