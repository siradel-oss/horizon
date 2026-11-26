// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/picking_types.h"
#include "hrz/common/vector_data/attribute_type_api.h"
#include "hrz/core/three_d_tiles/system.h"
#include "hrz/core/three_d_tiles/tile_types.h"
#include "hrz/fnd/log.h"

#include <cassert>
#include <optional>
#include <span>

// Let's talk about picking a bit.
//
// Object references use the following format:
// tttttttiiiiissss iiiiiiibbbbbbbbb
// ---------------- ----------------
// rrrrrrrrrrrrrrrr gggggggggggggggg
// layer_id         object_id
//
// Each character represents two bits. 64 bits in total.
// Least significant bits are on the right.
// s: System picking id
// t: Tileset handle
// i: Tile index
// b: batch id
// The batch id is added to the low picking id value in
// the fragment shader.
// So in the end we have:
//  * 8 bits, or 256 values, for the system id (we don't control that),
//  * 14 bits, or 16,384 values, for the tileset handle,
//  * 24 bits, or 16,777,216 values, for the tile index,
//  * 18 bits, or 262,144 values, for the batch id.
//
// In the model system and shaders, batch ids are simply added to the
// object_id part, so those bits need to be the lowest bits.
//
// Feature references simply use the local layer handle in the layer_id part.

namespace hrz::three_d_tiles
{

namespace
{

struct UpdateSelectedFeaturesVisitor
{
    void operator ()(Tileset& tileset, ThreeDTile&, ContentSubtile auto& content) const
    {
        content.update_selection(tileset.config->selected_feature_ids);
    }
};

} // anonymous namespace

hrz::picking::ObjectReference ThreeDTilesSystem::make_object_reference(
    uint8_t system_picking_id,
    TilesetH tileset_handle,
    uint32_t tile_index)
{
    hrz::picking::ObjectReference obj;
    obj.system_id = system_picking_id;
    obj.complementary_id = (tileset_handle << 10) + (tile_index >> 14);
    obj.object_id = ((tile_index & 0x3fff) << 18);
    return obj;
}

void ThreeDTilesSystem::extract_info_from_object_reference(
    const hrz::picking::ObjectReference& ref,
    TilesetH* out_tileset_handle,
    uint32_t* out_tile_index,
    uint32_t* out_batch_id)
{
    *out_tileset_handle = (ref.complementary_id & 0xfffc00) >> 10;
    *out_tile_index = ((ref.complementary_id & 0x3ff) << 14) + ((ref.object_id & 0xfffc0000) >> 18);
    *out_batch_id = ref.object_id & 0x3ffff;
}

void ThreeDTilesSystem::add_selected_features(
    TilesetH handle,
    std::span<const hrz::vector_data::FeatureIdHash> feature_ids)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    auto* config = tileset->get_mutable_config();

    for (const auto feature_id : feature_ids)
    {
        // feature_id == 0 represents an empty feature ID. If the layer
        // has no feature IDs, we don't want to select and highlight the
        // whole layer.
        if (feature_id != 0)
        {
            config->selected_feature_ids.insert(feature_id);
        }
    }

    visit_tiles(handle, UpdateSelectedFeaturesVisitor{});
}

void ThreeDTilesSystem::clear_selected_features(TilesetH handle)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    tileset->get_mutable_config()->selected_feature_ids.clear();
    visit_tiles(handle, UpdateSelectedFeaturesVisitor{});
}

std::optional<hrz::vector_data::FeatureId> ThreeDTilesSystem::get_feature_id(
    TilesetH tileset_handle,
    uint32_t tile_index,
    uint32_t batch_id)
{
    auto tileset = _tilesets.get_object(tileset_handle);

    if (tileset == nullptr)
    {
        HRZ_LOG_WARNING("Unknown tileset handle: {}", tileset_handle);
        return std::nullopt;
    }

    if (tile_index >= tileset->tiles.size())
    {
        HRZ_LOG_WARNING("Invalid tile index for tileset {}: {}", tileset_handle, tile_index);
        return std::nullopt;
    }

    const auto& tile = tileset->tiles.at(tile_index);

    for (const auto& subtile : tile.subtiles)
    {
        if (batch_id >= subtile.batch_id_offset
            && batch_id < subtile.batch_id_offset + subtile.batch_length)
        {
            if (subtile.batches_to_feature_ids.size() == 0)
            {
                return std::nullopt; // No feature ids
            }

            assert(subtile.batches_to_feature_ids.size() == subtile.batch_length);
            return {subtile.batches_to_feature_ids.at(batch_id - subtile.batch_id_offset)};
        }
    }

    return std::nullopt;
}

std::optional<ThreeDTilesSystem::AttributeValue> ThreeDTilesSystem::get_attribute_value(
    TilesetH tileset_handle,
    uint32_t tile_index,
    uint32_t batch_id,
    unsigned int attribute_index)
{
    auto tileset = _tilesets.get_object(tileset_handle);

    if (tileset == nullptr)
    {
        HRZ_LOG_WARNING("Unknown tileset handle: {}", tileset_handle);
        return std::nullopt;
    }

    if (tile_index >= tileset->tiles.size())
    {
        HRZ_LOG_WARNING("Invalid tile index for tileset {}: {}", tileset_handle, tile_index);
        return std::nullopt;
    }

    const auto& tile = tileset->tiles.at(tile_index);

    for (const auto& subtile : tile.subtiles)
    {
        if (batch_id < subtile.batch_id_offset
            || batch_id >= subtile.batch_id_offset + subtile.batch_length)
        {
            continue;
        }
        assert(batch_id < subtile.batch_length);

        if (attribute_index >= tileset->config->attributes.size())
        {
            HRZ_LOG_WARNING(
                "Invalid attribute index for tileset {}: {}", tileset_handle, attribute_index);
            return std::nullopt;
        }

        if (attribute_index >= subtile.attribute_values.size())
        {
            return std::nullopt;
        }

        auto& values_opt = subtile.attribute_values.at(attribute_index);

        if (!values_opt.has_value())
        {
            return std::nullopt;
        }

        auto& values = values_opt.value();
        assert(values.values.size() == subtile.batch_length);

        const auto& attribute = tileset->config->attributes.at(attribute_index);

        auto value = hrz::vector_data::attr_from<hrz::vector_data::ApiAttributeValue>(
            values.get_reader().as_ref(batch_id - subtile.batch_id_offset));

        return {{attribute.name, value}};
    }

    return std::nullopt;
}

} // namespace hrz::three_d_tiles
