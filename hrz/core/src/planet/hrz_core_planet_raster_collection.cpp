#include "planet/hrz_core_planet_raster_collection.h"

#include "hrz_core_loading_priorities.h"

#include <hrz_common_image_processing.h>
#include <hrz_common_proto_geo.h>

namespace hrz::planet
{
constexpr const char* ImageryRasterCollectionTraits::NAME;
constexpr hrz_proto::LayerType ImageryRasterCollectionTraits::LAYER_TYPE;
constexpr std::array<hrz_proto::ImageFormat, 1>
    ImageryRasterCollectionTraits::SOURCE_TILE_IMAGE_FORMATS;
constexpr hrz_proto::ImageFormat ImageryRasterCollectionTraits::COMPOSED_TILE_IMAGE_FORMAT;

constexpr const char* DtmRasterCollectionTraits::NAME;
constexpr hrz_proto::LayerType DtmRasterCollectionTraits::LAYER_TYPE;
constexpr std::array<hrz_proto::ImageFormat, 5>
    DtmRasterCollectionTraits::SOURCE_TILE_IMAGE_FORMATS;
constexpr hrz_proto::ImageFormat DtmRasterCollectionTraits::COMPOSED_TILE_IMAGE_FORMAT;

namespace details
{
assets_loader::Queue raster_group_to_priority_queue(hrz_proto::RasterGroup group)
{
    switch (group)
    {
        case hrz_proto::RasterGroup::BOTTOM_RASTER_GROUP:
            return assets_loader::Queue::ImageryBottom;
        case hrz_proto::RasterGroup::MIDDLE_RASTER_GROUP:
            return assets_loader::Queue::ImageryMiddle;
        case hrz_proto::RasterGroup::TOP_RASTER_GROUP: return assets_loader::Queue::ImageryTop;
        default:
            HRZ_LOG_WARNING(
                "No queue defined for {} raster group", hrz_proto::RasterGroup_Name(group));
            return assets_loader::Queue::ImageryBottom;
    }
}

assets_loader::Queue get_priority_queue(
    hrz_proto::LayerType layer_type,
    hrz_proto::RasterGroup group,
    int8_t loading_priority)
{
    return get_request_queue(
        loading_priority,
        layer_type == hrz_proto::LayerType::DTM_RASTER ? assets_loader::Queue::Dtm
                                                       : raster_group_to_priority_queue(group));
}

bool raster_model_is_complete(const hrz_proto::Raster& model, hrz_proto::LayerType layer_type)
{
    if (!model.has_provider()) return false;

    const auto& provider_model = model.provider();

    if (!is_provider_model_complete(provider_model)) return false;

    bool has_blending = layer_type == hrz_proto::LayerType::DTM_RASTER || model.has_blending();

    auto format = get_image_format(provider_model);
    bool has_correct_format =
        (layer_type == hrz_proto::LayerType::DTM_RASTER && hrz::is_scalar_image_format(format))
        || (layer_type == hrz_proto::LayerType::IMAGERY_RASTER
            && format == hrz_proto::ImageFormat::SRGBA_8);

    return has_blending && has_correct_format;
}

std::unique_ptr<RasterProvider> raster_provider_from_model(
    const hrz_proto::Raster& raster_model,
    const hrz_proto::LayerType& layer_type,
    const hrz_proto::RasterGroup& group,
    int8_t loading_priority,
    uint32_t default_tile_cache_size,
    uint64_t raster_id)
{
    const assets_loader::Queue download_queue =
        get_priority_queue(layer_type, group, loading_priority);

    std::unique_ptr<planet::RasterProvider> raster_provider = create_provider(
        raster_model.provider(), download_queue, default_tile_cache_size, raster_id);

    return raster_provider;
}

std::unique_ptr<Raster> raster_from_model(
    uint64_t raster_id,
    uint64_t unique_id,
    const hrz_proto::Raster& raster_model,
    uint32_t slot,
    const hrz_proto::RasterGroup& group,
    const hrz_proto::LayerType& layer_type,
    bool is_visible,
    const hrz_proto::LayerVisibilityConstraintList& visibility_constraints,
    uint32_t scene_views,
    uint32_t default_tile_cache_size)
{
    assert(
        layer_type == hrz_proto::LayerType::DTM_RASTER
        || layer_type == hrz_proto::LayerType::IMAGERY_RASTER);

    int8_t loading_priority = clamp_cast<int32_t, int8_t>(raster_model.loading_priority());

    auto raster_provider = raster_provider_from_model(
        raster_model, layer_type, group, loading_priority, default_tile_cache_size, raster_id);

    std::unique_ptr<planet::Raster> raster(new planet::Raster());
    raster->id = raster_id;
    raster->unique_id = unique_id;
    raster->is_visible = is_visible;
    raster->slot = slot;
    raster->type = layer_type;
    raster->raster_group = group;
    raster->provider = std::move(raster_provider);
    raster->display_bounds = details::project_to_web_mercator(raster_model.display_bounds());
    raster->blending.CopyFrom(raster_model.blending());
    raster->sampling.CopyFrom(raster_model.sampling());
    raster->loading_priority = loading_priority;
    raster->visibility_constraints = visibility_constraints;
    raster->scene_views = scene_views;

    return raster;
}

lm::dbbox2 project_to_web_mercator(const hrz_proto::GeographicBounds& wgs84_bounds)
{
    auto display_bounds = hrz::from_proto(wgs84_bounds);

    // Display bounds straddling the anti-meridian are not supported, but we can at least
    // display part of the desired bounds. Namely, the larger non-straddling part.
    while (display_bounds.east < display_bounds.west)
    {
        if (display_bounds.east - (-lm::PI) > lm::PI - display_bounds.west)
        {
            display_bounds.west = -lm::PI;
        }
        else
        {
            display_bounds.east = lm::PI;
        }
    }

    return {
        hrz::geo_to_web_mercator(GeoPosition2{display_bounds.south, display_bounds.west}),
        hrz::geo_to_web_mercator(GeoPosition2{display_bounds.north, display_bounds.east})};
}
} // namespace details
} // namespace hrz::planet
