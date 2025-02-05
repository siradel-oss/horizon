#include "hrz_scene_dump_utils.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/descriptor_database.h>
#include <google/protobuf/dynamic_message.h>
#include <lz4.h>

std::vector<std::byte> hrz::scene_dump::decompress(gsl::span<const std::byte> data)
{
    std::vector<std::byte> buffer(data.size() * 3);

    while (true)
    {
        int decompressed = LZ4_decompress_safe(
            (const char*)data.data(), (char*)buffer.data(), data.size(), buffer.size());
        if (decompressed >= 0)
        {
            buffer.resize(decompressed);
            break;
        }
        else
        {
            buffer.resize(buffer.size() * 2);
        }
    }

    return buffer;
}

std::vector<std::byte> hrz::scene_dump::read_file(const char* file_path)
{
    std::vector<std::byte> buffer;

    FILE* fp = fopen(file_path, "rb");
    if (!fp) return buffer;

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    buffer.resize(size);
    size = fread(buffer.data(), 1, buffer.size(), fp);
    buffer.resize(size);
    fclose(fp);

    return buffer;
}

bool hrz::scene_dump::write_file(const char* file, gsl::span<const std::byte> data)
{
    FILE* fp = fopen(file, "wb+");
    if (!fp) return false;

    fwrite(data.data(), data.size(), 1, fp);
    fclose(fp);

    return true;
}

static google::protobuf::FileDescriptorProto make_minimal_descriptor()
{
    google::protobuf::FileDescriptorProto descriptor;
    descriptor.set_syntax("proto3");
    descriptor.set_name("dump.proto");

    auto* dump_msg = descriptor.add_message_type();
    dump_msg->set_name("SceneDump");

    auto* version_field = dump_msg->add_field();
    version_field->set_name("version");
    version_field->set_number(2);
    version_field->set_type(google::protobuf::FieldDescriptorProto_Type_TYPE_UINT32);

    return descriptor;
}

uint32_t hrz::scene_dump::get_scene_dump_version(gsl::span<const std::byte> data)
{
    google::protobuf::SimpleDescriptorDatabase database;
    database.Add(make_minimal_descriptor());

    google::protobuf::DescriptorPool pool(&database);
    google::protobuf::DynamicMessageFactory factory(&pool);

    auto* msg = factory.GetPrototype(pool.FindMessageTypeByName("SceneDump"))->New();
    msg->ParseFromArray(data.data(), data.size());

    return msg->GetReflection()->GetUInt32(*msg, msg->GetDescriptor()->FindFieldByName("version"));
}

std::vector<std::byte> hrz::scene_dump::set_scene_dump_version(
    gsl::span<const std::byte> data,
    uint32_t version)
{
    google::protobuf::SimpleDescriptorDatabase database;
    database.Add(make_minimal_descriptor());

    google::protobuf::DescriptorPool pool(&database);
    google::protobuf::DynamicMessageFactory factory(&pool);

    auto* msg = factory.GetPrototype(pool.FindMessageTypeByName("SceneDump"))->New();
    msg->ParseFromArray(data.data(), data.size());
    msg->GetReflection()->SetUInt32(msg, msg->GetDescriptor()->FindFieldByName("version"), version);

    std::vector<std::byte> buffer(msg->ByteSizeLong());
    msg->SerializeToArray(buffer.data(), buffer.size());
    return buffer;
}
