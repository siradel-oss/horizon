#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>

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
        std::string file_name = file->name().substr(0, file->name().size() - 6) + ".pb.h";
        auto* stream = generator_context->OpenForInsert(file_name, "global_scope");
        google::protobuf::io::Printer printer(stream, '$');
        printer.Print("namespace hrz_proto { using namespace ::HrzProtocol; }\n");
        return true;
    }
};

int main(int argc, char* argv[])
{
    Generator generator;
    google::protobuf::compiler::PluginMain(argc, argv, &generator);
    return 0;
}
