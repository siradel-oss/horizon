#pragma once

#include <hrz_protocol_all.h>

#include <lin_maths.h>

#include <string_view>

namespace hrz_mapbox
{
struct TranslationSettings
{
    lm::dvec2 viewport_size;
    double camera_fovy = 0;
    hrz_proto::RasterGroup raster_group = hrz_proto::RasterGroup::BOTTOM_RASTER_GROUP;
    uint32_t first_raster_slot = 0;
    uint32_t first_vector_data_layer_id = 0;
    uint32_t first_flat_overlay_z_index = 0;
    uint32_t first_symbol_z_index = 0;
};

struct TranslationResult
{
    bool success = false;
    uint32_t raster_slots_used = 0;
    uint32_t vector_data_layer_ids_used = 0;
    uint32_t flat_overlay_z_indices_used = 0;
    uint32_t symbol_z_indices_used = 0;
};

// Tries to translate a Mapbox scene from the given JSON. Returns the result as a scene dump.
TranslationResult translate_scene(
    std::string_view json,
    std::string_view sprite_index_json,
    const TranslationSettings&,
    hrz_proto::SceneDump* scene_dump);
} // namespace hrz_mapbox
