#include "hrz/scene_dump/migration.h"

#include "hrz/protocol/descriptor_sets_list.h"
#include "hrz/scene_dump/dynamic_message.h"
#include "hrz/scene_dump/migrations_list.h"
#include "hrz/scene_dump/utils.h"

#include <memory>
#include <optional>

using namespace hrz;
using namespace migration;

std::optional<google::protobuf::FileDescriptorSet> find_descriptor_set(uint32_t version_id)
{
    for (size_t i = 0; i < DescriptorSetCount; ++i)
    {
        if (DescriptorSets[i].id == version_id)
        {
            std::vector<std::byte> data =
                hrz::scene_dump::decompress(DescriptorSets[i].descriptor_set);
            google::protobuf::FileDescriptorSet desc;
            (void)desc.ParseFromArray(data.data(), data.size());
            return desc;
        }
    }
    return std::nullopt;
}

std::unique_ptr<google::protobuf::Message> create_scene_dump(
    hrz::migration::MessageFactory* factory,
    std::span<const std::byte> data = {})
{
    const auto* descriptor = factory->pool.FindMessageTypeByName("HrzProtocol.SceneDump");
    assert(descriptor);

    auto* msg = factory->factory.GetPrototype(descriptor)->New();
    assert(msg);

    if (!data.empty())
    {
        (void)msg->ParseFromArray(data.data(), data.size());
    }

    return std::unique_ptr<google::protobuf::Message>(msg);
}

std::vector<std::byte> serialize(const google::protobuf::Message& msg)
{
    std::vector<std::byte> buffer(msg.ByteSizeLong());
    (void)msg.SerializeToArray(buffer.data(), buffer.size());
    return buffer;
}

std::unique_ptr<MessageFactory> try_make_factory(uint32_t version_id)
{
    auto desc_opt = find_descriptor_set(version_id);
    if (desc_opt.has_value())
    {
        return std::make_unique<MessageFactory>(std::move(desc_opt.value()));
    }
    else
    {
        return nullptr;
    }
}

std::vector<std::byte> hrz::migration::migrate(std::vector<std::byte> data, int* applied)
{
    if (applied) *applied = 0;

    initialize_descriptor_sets();

    if (data.empty())
    {
        printf("Empty data\n");
        return {};
    }

    uint32_t version_from = hrz::scene_dump::get_scene_dump_version(data);

    std::optional<size_t> version_from_index;
    for (size_t i = 0; i < MigrationCount; ++i)
    {
        if (Migrations[i].id == version_from)
        {
            version_from_index = i;
            break;
        }
    }

    if (!version_from_index.has_value())
    {
        printf("Version not found\n");
        return {};
    }

    for (size_t i = version_from_index.value(); i < MigrationCount - 1; ++i)
    {
        uint32_t version_src = Migrations[i].id;
        uint32_t version_dst = Migrations[i + 1].id;
        assert(Migrations[i].function);

        auto factory_src = try_make_factory(version_src);
        assert(factory_src);

        auto factory_dst = try_make_factory(version_dst);
        assert(factory_dst);

        auto dump_src = create_scene_dump(factory_src.get(), data);
        auto dump_dst = create_scene_dump(factory_dst.get(), data);

        DynamicMessage msg_src(factory_src.get(), dump_src.get());
        DynamicMessage msg_dst(factory_dst.get(), dump_dst.get());

        msg_dst.set_uint32("version", version_dst);
        if (!Migrations[i].function(msg_src, &msg_dst))
        {
            printf("Error during migration from %08x to %08x\n", version_src, version_dst);
            return {};
        }

        // Last migration: discard unknown fields. We don't do that before
        // because in the rebasing/merging workflow (using tools/scene_dump/set_dump_version.py), we
        // might have fields we don't want to overwrite.
        if (i == MigrationCount - 2)
        {
            msg_dst.msg->DiscardUnknownFields();
        }

        data = serialize(*msg_dst.msg);
        if (applied) *applied += 1;
    }

    return data;
}
