#include "hrz_mapbox.h"

#include "hrz_mapbox_common.h"
#include "hrz_mapbox_expression.h"

#include <hrz_common_color.h>
#include <hrz_common_geo.h>
#include <hrz_common_proto_settings.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_json_utils.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_static_vector.h>
#include <hrz_fnd_time.h>
#include <hrz_fnd_variant.h>
#include <hrz_scene_model_version.h>

#include <lin_maths.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <string.h>

#include <bit>
#include <string>

#define CHECK_ERR_M(MSG, ...)   \
    do                          \
    {                           \
        if (!(__VA_ARGS__))     \
        {                       \
            HRZ_LOG_ERROR(MSG); \
            return false;       \
        }                       \
    } while (0)

#define CHECK_ERR(...)      \
    do                      \
    {                       \
        if (!(__VA_ARGS__)) \
        {                   \
            return false;   \
        }                   \
    } while (0)

namespace hrz_mapbox
{
using namespace hrz;

namespace
{
static constexpr uint8_t MapboxMinSupportedVersion = 8;
static constexpr uint8_t MapboxMaxSupportedVersion = 8;

struct MetadataAttribute
{
    const char* attribute_name;
};

using SourcePrototype = std::variant<hrz_proto::ImageryRasterLayer, hrz_proto::VectorDataLayer>;

static constexpr size_t RasterSourcePrototypeIndex =
    hrz::index_of_variant<SourcePrototype, hrz_proto::ImageryRasterLayer>();
static constexpr size_t VectorDataSourcePrototypeIndex =
    hrz::index_of_variant<SourcePrototype, hrz_proto::VectorDataLayer>();

using VectorSourceHash = uint64_t;

VectorSourceHash hash_vector_source(
    std::string_view source_name,
    std::string_view source_layer_name)
{
    return hrz::hash_mix(hrz::murmur3_x64_64(source_name), hrz::murmur3_x64_64(source_layer_name));
}

struct ParseContext
{
    hrz_proto::SceneDump* scene_dump;
    const TranslationSettings* settings;

    hrz::flat_hash_map<std::string, SourcePrototype> source_names_to_prototypes;
    hrz::flat_hash_map<VectorSourceHash, VectorSource> vector_source_hashes_to_data;

    hrz::flat_hash_map<std::string, std::string> font_names_to_urls;
    std::string sprite_image_url;

    uint32_t next_raster_slot = 0;
    uint32_t next_vector_data_layer_id = 0;
    uint32_t next_flat_overlay_z_index = 0;
    uint32_t next_symbol_z_index = 0;
};

bool warn_unsupported_member(const rapidjson::Value& value, const char* name)
{
    if (value.IsObject() && value.HasMember(name))
    {
        HRZ_LOG_WARNING("Horizon does not support the {} property", name);
        return true;
    }
    return false;
};

// Compute the camera distance so that a tile of the given zoom level appears at a size of
// 512 pixels on screen
double approximate_camera_distance(
    double lat_rad,
    double zoom_level,
    lm::dvec2 viewport_size,
    double fovy)
{
    // Based on https://wiki.openstreetmap.org/wiki/Zoom_levels "Distance per pixel math"
    // We clamp the zoom level at low levels because it doesn't make sense to go below a certain
    // threshold since our view is 3D and thus we can never see the whole Earth at once.
    double tile_size_meters =
        hrz::EARTH_CIRCUMFERENCE * std::cos(lat_rad) / std::pow(2.0, std::max(2.5, zoom_level));
    return tile_size_meters / 512.0 * viewport_size.y / 2.0 / std::tan(fovy * 0.5);
}

hrz_proto::GeographicBounds parse_bounds(const rapidjson::Value& object)
{
    hrz_proto::GeographicBounds bounds;
    bounds.set_west(
        json::as_double(json::get_nth_member_or_null(object, "bounds", 0)).value_or(-180.0));
    bounds.set_south(json::as_double(json::get_nth_member_or_null(object, "bounds", 1))
                         .value_or(-MERCATOR_MAX_LAT_DEG));
    bounds.set_east(
        json::as_double(json::get_nth_member_or_null(object, "bounds", 2)).value_or(180.0));
    bounds.set_north(json::as_double(json::get_nth_member_or_null(object, "bounds", 3))
                         .value_or(MERCATOR_MAX_LAT_DEG));
    return bounds;
}

std::optional<const char*> parse_endpoint(const char* source_name, const rapidjson::Value& object)
{
    auto tiles_size = json::get_array_size(object, "tiles");
    if (tiles_size == 0)
    {
        HRZ_LOG_ERROR("Missing endpoint for source {}", source_name);
        return std::nullopt;
    }
    else if (tiles_size > 1)
    {
        HRZ_LOG_INFO(
            "Multiple endpoints aren't supported yet. Only the first specified endpoint will be "
            "used.");
    }
    return json::as_str(json::get_nth_member_or_null(object, "tiles", 0));
}

bool parse_raster_source(ParseContext* ctx, const char* source_name, const rapidjson::Value& source)
{
    // Raster source
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/sources/#raster

    hrz_proto::ImageryRasterLayer raster_layer;
    auto* raster = raster_layer.mutable_raster();
    auto* sampling_params = raster->mutable_sampling();
    auto* blending_params = raster->mutable_blending();

    hrz_proto::GeographicBounds bounds = parse_bounds(source);
    raster->mutable_display_bounds()->CopyFrom(bounds);

    sampling_params->set_alpha_channel_usage(hrz_proto::USE_ALPHA_CHANNEL);
    sampling_params->set_filtering(hrz_proto::NEAREST);

    blending_params->set_opacity(1.0);

    // @Todo(qdebroise): decipher mapbox:// urls.
    auto url = json::get_str(source, "url");
    if (url.has_value())
    {
        if (hrz::str::starts_with(url.value(), "pmtiles://"))
        {
            raster->mutable_provider()->set_type(
                hrz_proto::RasterProviderType::PMTILES_RASTER_PROVIDER);
            auto* pmtiles_provider = raster->mutable_provider()->mutable_pmtiles();
            pmtiles_provider->set_url(std::string(std::string_view(url.value()).substr(10)));
        }
        else
        {
            // Rely on the TileJSON provider.
            // @Todo: "Explicit source options take precedence over TileJSON"
            // (https://github.com/mapbox/mapbox-gl-js/blob/d7aeb4b764d6bbaa98b03d9e8abf1a5d673189ff/src/source/load_tilejson.js#L22).
            raster->mutable_provider()->set_type(
                hrz_proto::RasterProviderType::TILEJSON_RASTER_PROVIDER);
            auto* tilejson_provider = raster->mutable_provider()->mutable_tilejson();

            tilejson_provider->set_url(url.value());
        }
    }
    else
    {
        // Manually fill a tiled provider.
        raster->mutable_provider()->set_type(hrz_proto::RasterProviderType::TILED_RASTER_PROVIDER);
        auto* tiled_provider = raster->mutable_provider()->mutable_tiled();

        // Mapbox sets a default zoom level of 22. The TileJSON spec requires the zoom levels to
        // be in [0, 30].
        // - https://github.com/mapbox/tilejson-spec/tree/master/3.0.0#312-maxzoom
        // - https://docs.mapbox.com/mapbox-gl-js/style-spec/sources/#raster-maxzoom
        auto min_zoom = hrz::clamp(json::get_int_or(source, "minzoom", 0), 0, 30);
        auto max_zoom = hrz::clamp(json::get_int_or(source, "maxzoom", 22), min_zoom, 30);
        auto tile_size = json::get_int_or(source, "tileSize", 512);
        auto endpoint = parse_endpoint(source_name, source);
        if (!endpoint.has_value())
        {
            return false;
        }

        auto* geometry = tiled_provider->mutable_geometry();

        auto* projection = geometry->mutable_projection();
        projection->set_descriptor_type(hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR);
        projection->set_descriptor_("EPSG:3857");

        auto* tiling_scheme = tiled_provider->mutable_tiling_scheme();
        tiling_scheme->set_type(hrz_proto::GLOBAL);
        tiling_scheme->mutable_global_tiling()->set_min_level(min_zoom);
        tiling_scheme->mutable_global_tiling()->set_max_level(max_zoom);
        tiling_scheme->mutable_global_tiling()->set_tile_size(tile_size);
        tiling_scheme->mutable_global_tiling()->set_level_zero_tile_count_x(1);
        tiling_scheme->mutable_global_tiling()->set_level_zero_tile_count_y(1);
        tiling_scheme->mutable_global_tiling()->set_border_tile_aspect(hrz_proto::FULL_SIZED);

        // @Todo(HRZ-980): support multiple endpoints.
        tiled_provider->set_url_pattern(endpoint.value());
    }

    ctx->source_names_to_prototypes.insert({source_name, std::move(raster_layer)});

    warn_unsupported_member(source, "attribution");
    warn_unsupported_member(source, "scheme");
    warn_unsupported_member(source, "volatile");

    return true;
}

bool parse_vector_source(ParseContext* ctx, const char* source_name, const rapidjson::Value& source)
{
    // Vector source
    // https://docs.mapbox.com/style-spec/reference/sources/#vector

    hrz_proto::VectorDataLayer vector_data_layer;
    hrz_proto::VectorDataSource* vector_data_source = vector_data_layer.add_sources();

    auto url = json::get_str(source, "url");

    vector_data_source->set_has_geometry(true);

    if (url.has_value())
    {
        if (hrz::str::starts_with(url.value(), "pmtiles://"))
        {
            vector_data_source->set_provider_type(
                hrz_proto::VectorDataProviderType::PMTILES_VECTOR_DATA_PROVIDER);
            auto* pmtiles_provider = vector_data_source->mutable_pmtiles_data_provider();

            pmtiles_provider->set_url(std::string(std::string_view(url.value()).substr(10)));
        }
        else
        {
            // Rely on the TileJSON provider.
            vector_data_source->set_provider_type(
                hrz_proto::VectorDataProviderType::TILEJSON_VECTOR_DATA_PROVIDER);
            auto* tilejson_provider = vector_data_source->mutable_tilejson_data_provider();

            tilejson_provider->set_url(url.value());
        }
    }
    else
    {
        // Manually fill a tiled provider.
        vector_data_source->set_provider_type(
            hrz_proto::VectorDataProviderType::TILED_VECTOR_DATA_PROVIDER);
        auto* tiled_provider = vector_data_source->mutable_tiled_data_provider();

        auto endpoint = parse_endpoint(source_name, source);
        if (!endpoint.has_value())
        {
            return false;
        }

        tiled_provider->set_url_pattern(endpoint.value());
        tiled_provider->set_format(hrz_proto::VectorDataFormat::MVT_VECTOR_DATA);

        // @Todo(qdebroise): Maputnik exports "minZoom" and "maxZoom" which isn't spec compliant.
        auto min_level = hrz::clamp(json::get_int_or(source, "minzoom", 0), 0, 30);
        auto max_level = hrz::clamp(json::get_int_or(source, "maxzoom", 22), min_level, 30);

        hrz_proto::GeographicBounds bounds = parse_bounds(source);

        tiled_provider->set_min_level(min_level);
        tiled_provider->set_max_level(max_level);
        tiled_provider->mutable_bounds()->CopyFrom(bounds);
    }

    ctx->source_names_to_prototypes.insert({source_name, std::move(vector_data_layer)});

    warn_unsupported_member(source, "attribution");
    warn_unsupported_member(source, "volatile");
    // @Todo(qdebroise): See about feature ID attributes which are declared here.
    // https://docs.mapbox.com/style-spec/reference/sources/#vector-promoteId
    warn_unsupported_member(source, "promoteId");

    return true;
}

bool parse_geojson_source(
    ParseContext* ctx,
    const char* source_name,
    const rapidjson::Value& source)
{
    // Geojson source
    // https://docs.mapbox.com/style-spec/reference/sources/#geojson

    hrz_proto::VectorDataLayer vector_data_layer;
    hrz_proto::VectorDataSource* vector_data_source = vector_data_layer.add_sources();

    vector_data_source->set_has_geometry(true);
    vector_data_source->set_provider_type(
        hrz_proto::VectorDataProviderType::UNTILED_VECTOR_DATA_PROVIDER);

    auto* provider = vector_data_source->mutable_untiled_data_provider();
    provider->set_format(hrz_proto::VectorDataFormat::GEOJSON_VECTOR_DATA);

    auto min_level_opt = hrz::json::get_int(source, "minzoom");
    if (min_level_opt.has_value())
    {
        provider->set_min_level(min_level_opt.value());
    }

    auto max_level_opt = hrz::json::get_int(source, "maxzoom");
    provider->set_max_level(max_level_opt.value_or(22));

    auto bounds = provider->mutable_bounds();
    bounds->set_west(-180.0);
    bounds->set_south(-85.051129);
    bounds->set_east(180.0);
    bounds->set_north(85.051129);

    auto buffer = hrz::json::get_float_or(source, "buffer", 128.0);
    provider->set_clip_margin(buffer > 0.0);

    auto url_opt = json::get_str(source, "data");
    if (url_opt.has_value())
    {
        provider->set_url(url_opt.value());
    }
    else
    {
        const auto& inline_geojson = json::get_member_or_null(source, "data");
        if (!inline_geojson.IsObject())
        {
            HRZ_LOG_ERROR("Could not parse \"data\" property of geojson source");
            return false;
        }

        // The easiest way to handle inline geojson is to turn it into a data URL.
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        inline_geojson.Accept(writer);

        const char* geojson_str = buffer.GetString();

        provider->set_url("data:;base64,");
        hrz::str::encode_base64(
            std::as_bytes(std::span<const char>(geojson_str, buffer.GetSize())),
            provider->mutable_url());
    }

    ctx->source_names_to_prototypes.insert({source_name, std::move(vector_data_layer)});

    warn_unsupported_member(source, "attribution");
    warn_unsupported_member(source, "cluster");
    warn_unsupported_member(source, "clusterMaxZoom");
    warn_unsupported_member(source, "clusterMinPoints");
    warn_unsupported_member(source, "clusterProperties");
    warn_unsupported_member(source, "clusterRadius");
    warn_unsupported_member(source, "filter");
    warn_unsupported_member(source, "lineMetrics");
    // @Todo: feature IDs
    warn_unsupported_member(source, "promoteId");
    warn_unsupported_member(source, "generateId");
    warn_unsupported_member(source, "tolerance");

    return true;
}

bool parse_source(ParseContext* ctx, const char* source_name, const rapidjson::Value& source)
{
    // Sources
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/sources/

    CHECK_ERR_M("Source is not an object.", source.IsObject());
    auto type = json::get_str(source, "type");
    CHECK_ERR_M("Source has no type.", type.has_value());

    if (strcmp(type.value(), "raster") == 0)
    {
        CHECK_ERR(parse_raster_source(ctx, source_name, source));
    }
    else if (strcmp(type.value(), "vector") == 0)
    {
        CHECK_ERR(parse_vector_source(ctx, source_name, source));
    }
    else if (strcmp(type.value(), "geojson") == 0)
    {
        CHECK_ERR(parse_geojson_source(ctx, source_name, source));
    }
    else
    {
        HRZ_LOG_WARNING("Unsupported '{}' source type.", type.value());
    }

    return true;
}

bool parse_common_layer_properties(
    ParseContext* ctx,
    const rapidjson::Value& json_layer,
    hrz_proto::LayerVisibilityConstraintList* visibility_constraints,
    bool* visibility)
{
    auto max_zoom = hrz::clamp(json::get_int_or(json_layer, "maxzoom", 24), 0, 30);
    if (max_zoom < 24)
    {
        // This altitude corresponds to the altitude of the Mapbox camera with a pitch of 0 at
        // the given zoom level.
        const double altitude = approximate_camera_distance(
            lm::radians(ctx->scene_dump->cameras(0).viewpoint().target().latitude()), max_zoom,
            ctx->settings->viewport_size, ctx->settings->camera_fovy);

        auto* constraint = visibility_constraints->add_constraints();
        constraint->set_type(hrz_proto::ALTITUDE);
        constraint->mutable_altitude()->set_altitude(altitude);
        constraint->mutable_altitude()->set_relative_position(hrz_proto::ABOVE);
    }

    auto min_zoom = hrz::clamp(json::get_int_or(json_layer, "minzoom", 0), 0, 30);
    if (min_zoom > 0)
    {
        const double altitude = approximate_camera_distance(
            lm::radians(ctx->scene_dump->cameras(0).viewpoint().target().latitude()), min_zoom,
            ctx->settings->viewport_size, ctx->settings->camera_fovy);

        auto* constraint = visibility_constraints->add_constraints();
        constraint->set_type(hrz_proto::ALTITUDE);
        constraint->mutable_altitude()->set_altitude(altitude);
        constraint->mutable_altitude()->set_relative_position(hrz_proto::BELOW);
    }

    auto visibility_str = std::string_view(json::get_str_or(json_layer, "visibility", "visible"));
    if (visibility_str == "visible")
    {
        *visibility = true;
    }
    else if (visibility_str == "none")
    {
        *visibility = false;
    }

    return true;
}

bool parse_raster_layer(ParseContext* ctx, const char* layer_id, const rapidjson::Value& json_layer)
{
    // Raster layers
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/#raster

    auto source_name = json::get_str_or(json_layer, "source", "");

    auto source_it = ctx->source_names_to_prototypes.find(source_name);
    if (source_it == ctx->source_names_to_prototypes.end())
    {
        HRZ_LOG_ERROR("Missing source \"{}\" for raster layer \"{}\".", source_name, layer_id);
        return false;
    }

    const SourcePrototype& source = source_it->second;
    if (source.index() != RasterSourcePrototypeIndex)
    {
        HRZ_LOG_ERROR(
            "Raster layer \"{}\" requires its source \"{}\" to be of type 'raster'.", layer_id,
            source_name);
        return false;
    }

    const auto& prototype = std::get<hrz_proto::ImageryRasterLayer>(source);

    auto* layer = ctx->scene_dump->add_layers();
    layer->set_name(layer_id);

    auto* raster_layer = layer->mutable_imagery_raster();
    raster_layer->CopyFrom(prototype);

    raster_layer->set_group(ctx->settings->raster_group);
    raster_layer->set_slot(ctx->next_raster_slot++);

    bool visible = true;
    CHECK_ERR(parse_common_layer_properties(
        ctx, json_layer, raster_layer->mutable_visibility_constraints(), &visible));

    raster_layer->set_visible(visible);

    {
        static const json::EnumVariant<hrz_proto::TextureFiltering> filtering_variants[] = {
            {"nearest", hrz_proto::NEAREST},
            {"linear", hrz_proto::BILINEAR}};

        const auto& paint = json::get_member_or_null(json_layer, "paint");
        auto filtering = json::get_str_enum_or<hrz_proto::TextureFiltering>(
            paint, "raster-resampling", filtering_variants, hrz_proto::BILINEAR);

        auto* raster = raster_layer->mutable_raster();
        raster->mutable_sampling()->set_filtering(filtering);
        raster->mutable_blending()->set_opacity(json::get_double_or(paint, "raster-opacity", 1.0));

        warn_unsupported_member(json_layer, "raster-brightness-max");
        warn_unsupported_member(json_layer, "raster-brightness-min");
        warn_unsupported_member(json_layer, "raster-contrast");
        warn_unsupported_member(json_layer, "raster-fade-duration");
        warn_unsupported_member(json_layer, "raster-hue-rotate");
        warn_unsupported_member(json_layer, "raster-saturation");
    }

    {
        static const json::EnumVariant<bool> visibility_variants[] = {
            {"visible", true},
            {"none", false}};

        const auto& layout = json::get_member_or_null(json_layer, "layout");
        auto visibility =
            json::get_str_enum_or<bool>(layout, "visibility", visibility_variants, true);

        raster_layer->set_visible(visibility);
        raster_layer->mutable_scene_views()->set_bits((visibility) ? (1 | 2) : 0);
    }

    warn_unsupported_member(json_layer, "filter");

    return true;
}

NodeIndex parse_expression_or_array_literal(
    const rapidjson::Value& json,
    MapboxPropertyType expected_type,
    ExpressionContext& ctx)
{
    NodeIndex expression_index = parse_expression(json, expected_type, ctx);
    if (expression_index != NO_NODE)
    {
        return expression_index;
    }

    // Arrays should be wrapped in a "literal" operator inside an expression.
    // But properties with an array type also accept a literal array without the need
    // for the "literal" operator, as we currently are not inside an expression in that case.
    // https://docs.mapbox.com/style-spec/reference/types/#array
    auto component_type = mapbox_property_type_array_to_component_type(expected_type);
    if (component_type.has_value())
    {
        return parse_array_literal(json, component_type.value(), ctx);
    }

    return 0;
}

void parse_generic_property(
    const char* prp_name,
    MapboxPropertyType expected_type,
    const rapidjson::Value& json,
    Property::Generic* dst,
    ExpressionContext& ctx)
{
    if (json.IsNull()) return;

    const auto& prp_node = json::get_member_or_null(json, prp_name);

    if (prp_node.IsNull()) return;

    // @Todo(qdebroise): Do something about type checking and type inferring.

    NodeIndex expression_index = parse_expression_or_array_literal(prp_node, expected_type, ctx);
    if (expression_index == NO_NODE)
    {
        // When this happens we still want the layer to be created so the error is logged
        // but we don't propagate the error.
        HRZ_LOG_ERROR(
            "Failed to retrieve value for property '{}' of layer '{}'.", prp_name,
            ctx.mapbox_layer_id);
        return;
    }

    // Array nodes need to be handled carefully because they cannot reach the styling script
    // generation step. Allowing the use of operators with them expressions would cause all kinds
    // of headaches...
    bool expected_array = mapbox_property_type_array_to_component_type(expected_type).has_value();
    if (expected_array && ctx.nodes[expression_index].type != Node::Type::Array)
    {
        HRZ_LOG_ERROR(
            "Expressions are unsupported for array properties ('{}' of layer '{}')", prp_name,
            ctx.mapbox_layer_id);
        return;
    }

    if (ctx.nodes[expression_index].type == Node::Type::Literal)
    {
        // Override the default value by the one provided in script. This new value will be used
        // as the new default value of the property.
        dst->default_value = ctx.nodes[expression_index].literal;
    }
    else
    {
        // We don't want to assign `root_node` when the expression is only a single literal.
        // This way the root node can be left to an unused state and facilitate later
        // operations.
        dst->node = expression_index;
    }
}

void parse_to_string_property(
    const char* prp_name,
    MapboxPropertyType expected_type,
    const rapidjson::Value& json,
    Property::Generic* dst,
    ExpressionContext& ctx)
{
    parse_generic_property(prp_name, expected_type, json, dst, ctx);

    if (dst->node != NO_NODE)
    {
        dst->node = ctx.add_node(Node::Type::ToString, {dst->node});
    }
}

template<typename T>
void parse_numeric_property(
    const char* prp_name,
    MapboxPropertyType expected_type,
    std::optional<T> multiplier,
    std::optional<T> min,
    std::optional<T> max,
    const rapidjson::Value& json,
    Property::Generic* dst,
    ExpressionContext& ctx)
{
    assert(!mapbox_property_type_array_to_component_type(expected_type).has_value());

    if (json.IsNull()) return;

    const auto& prp_node = json::get_member_or_null(json, prp_name);

    if (prp_node.IsNull()) return;

    NodeIndex expression_index = parse_expression(prp_node, expected_type, ctx);
    if (expression_index == NO_NODE)
    {
        // When this happens we still want the layer to be created so the error is logged
        // but we don't propagate the error.
        HRZ_LOG_ERROR(
            "Failed to retrieve value for property '{}' of layer '{}'.", prp_name,
            ctx.mapbox_layer_id);
        return;
    }

    bool is_literal = ctx.nodes[expression_index].type == Node::Type::Literal;
    if (is_literal)
    {
        // Override the default value by the one provided in script. This new value will be used
        // as the new default value of the property.
        T value = get_value<T>(ctx.nodes[expression_index].literal) * multiplier.value_or(1);
        if (min.has_value())
        {
            value = std::max(value, min.value());
        }
        if (max.has_value())
        {
            value = std::min(value, max.value());
        }
        set_value<T>(dst->default_value, value);

        // Discard the literal node.
        assert(expression_index == ctx.nodes.size() - 1);
        ctx.nodes.resize(expression_index);
    }
    else
    {
        if (multiplier.has_value())
        {
            NodeIndex multiplier_node_index = ctx.add_literal({});
            ctx.nodes[multiplier_node_index].literal.type = get_value_type<T>();
            set_value<T>(ctx.nodes[multiplier_node_index].literal, multiplier.value());

            expression_index =
                ctx.add_node(Node::Type::Multiply, {multiplier_node_index, expression_index});
        }

        if (min.has_value())
        {
            NodeIndex min_value_node_index = ctx.add_literal({});
            ctx.nodes[min_value_node_index].literal.type = get_value_type<T>();
            set_value<T>(ctx.nodes[min_value_node_index].literal, min.value());

            expression_index =
                ctx.add_node(Node::Type::Max, {min_value_node_index, expression_index});
        }
        if (max.has_value())
        {
            NodeIndex max_value_node_index = ctx.add_literal({});
            ctx.nodes[max_value_node_index].literal.type = get_value_type<T>();
            set_value<T>(ctx.nodes[max_value_node_index].literal, max.value());

            expression_index =
                ctx.add_node(Node::Type::Max, {max_value_node_index, expression_index});
        }

        dst->node = expression_index;
    }
}

void parse_literal_property(
    const char* prp_name,
    MapboxPropertyType expected_type,
    const rapidjson::Value& paint_node,
    Value* dst,
    ExpressionContext& ctx)
{
    Property::Generic generic(*dst);
    parse_generic_property(prp_name, expected_type, paint_node, &generic, ctx);

    if (!generic.is_literal())
    {
        HRZ_LOG_WARNING("Horizon does not support expressions for the \"\" property", prp_name);
    }

    *dst = generic.default_value;
}

NodeIndex get_or_create_node(const Property::Generic& generic, ExpressionContext& ctx)
{
    if (generic.node == NO_NODE)
    {
        return ctx.add_literal(generic.default_value);
    }
    return generic.node;
};

// Mapbox properties that map to Horizon vector properties are usually expressed as an array
// of numbers.
void unwrap_array_properties(
    const Property::Generic& array,
    std::initializer_list<Property::Generic*> properties,
    ExpressionContext& expr)
{
    if (array.node != 0 && expr.nodes.at(array.node).type == Node::Type::Array)
    {
        const auto& array_node = expr.nodes.at(array.node);
        const size_t property_count = std::min(properties.size(), array_node.children.size());

        auto property_it = properties.begin();
        for (size_t i = 0; i < property_count; ++i)
        {
            auto* property = *property_it;
            property->node = array_node.children.at(i);

            const auto& node = expr.nodes.at(property->node);
            if (node.type == Node::Type::Literal)
            {
                property->node = 0;
                property->default_value = node.literal;
            }

            property_it++;
        }
    }
    else
    {
        assert(false && "Not an array node");
    }
}

class PropertyAdder
{
    std::vector<Property>* _properties;
    std::string_view _repr_name;

public:
    PropertyAdder(std::vector<Property>* properties, const char* repr_name) :
        _properties(properties), _repr_name(repr_name)
    {
    }

    template<typename PropertyValue, typename ProtoProperty>
    void add(std::string_view name, const PropertyValue& value, ProtoProperty* proto)
    {
        // Always try to assign the default value, as in the case of vector properties,
        // some components can be literal and other not.
        assign_default_value(value, proto);

        if (!value.is_literal())
        {
            _properties->push_back(Property(fmt::format("{}:{}", _repr_name, name), value));
            proto->set_name(_properties->back().styling_name);
        }
    }

    void add_extruded_vector_color(
        std::string_view name,
        const Property::ExtrudedVectorColor& value,
        hrz_proto::ColorProperty* roof_proto,
        hrz_proto::ColorProperty* upper_proto,
        hrz_proto::ColorProperty* lower_proto)
    {
        Property::ColorWithOpacity color_with_opacity(value.color, value.opacity);

        assign_default_value(color_with_opacity, roof_proto);
        assign_default_value(color_with_opacity, upper_proto);
        assign_default_value(color_with_opacity, lower_proto);

        if (value.is_never_gradient())
        {
            // This is the easier case: the "vertical gradient" option is always false,
            // so we can handle this as any other color property.
            if (!color_with_opacity.is_literal())
            {
                _properties->push_back(
                    Property(fmt::format("{}:{}", _repr_name, name), color_with_opacity));
                roof_proto->set_name(_properties->back().styling_name);
                upper_proto->set_name(_properties->back().styling_name);
                lower_proto->set_name(_properties->back().styling_name);
            }
        }
        else
        {
            // This is the harder case: the "vertical gradient" can be true. The lightening
            // and darkening of colors will be handled in the styling script, which is why
            // we must use a dedicated property type.
            std::string base_styling_name = fmt::format("{}:{}", _repr_name, name);
            _properties->push_back(Property(base_styling_name, value));

            std::string roof_styling_name;
            std::string upper_styling_name;
            std::string lower_styling_name;

            Property::ExtrudedVectorColor::generate_separate_styling_names(
                base_styling_name, &roof_styling_name, &upper_styling_name, &lower_styling_name);

            roof_proto->set_name(roof_styling_name);
            upper_proto->set_name(upper_styling_name);
            lower_proto->set_name(lower_styling_name);
        }
    }
};

// https://docs.mapbox.com/style-spec/reference/sprite/
bool parse_mapbox_sprite(
    const char* sprite_name,
    const rapidjson::Value& mapbox_sprite,
    hrz_proto::ImageSymbolElement* image)
{
    if (!mapbox_sprite.IsObject())
    {
        return false;
    }

    auto width_opt = hrz::json::get_float(mapbox_sprite, "width");
    if (!width_opt)
    {
        HRZ_LOG_ERROR("Sprite {} is missing required property \"width\"", sprite_name);
        return false;
    }

    auto height_opt = hrz::json::get_float(mapbox_sprite, "height");
    if (!height_opt)
    {
        HRZ_LOG_ERROR("Sprite {} is missing required property \"height\"", sprite_name);
        return false;
    }

    auto x_opt = hrz::json::get_float(mapbox_sprite, "x");
    if (!x_opt)
    {
        HRZ_LOG_ERROR("Sprite {} is missing required property \"x\"", sprite_name);
        return false;
    }

    auto y_opt = hrz::json::get_float(mapbox_sprite, "y");
    if (!y_opt)
    {
        HRZ_LOG_ERROR("Sprite {} is missing required property \"y\"", sprite_name);
        return false;
    }

    // `pixelRatio` is required by Mapbox but unused by Horizon, so we can display the sprite
    // anyway.
    if (!hrz::json::get_float(mapbox_sprite, "pixelRatio").has_value())
    {
        HRZ_LOG_WARNING(
            "Sprite {} is missing required (but unused) property \"pixelRatio\"", sprite_name);
    }

    auto* sprite = image->add_sprites();
    sprite->set_name(sprite_name);
    sprite->mutable_size()->set_x(width_opt.value());
    sprite->mutable_size()->set_y(height_opt.value());
    sprite->mutable_offset()->set_x(x_opt.value());
    sprite->mutable_offset()->set_y(y_opt.value());

    const auto& content = hrz::json::get_member_or_null(mapbox_sprite, "content");
    if (!content.IsNull())
    {
        if (content.IsArray() && content.Size() == 4)
        {
            sprite->mutable_content()->set_x_min(content.GetArray()[0].GetFloat());
            sprite->mutable_content()->set_y_min(content.GetArray()[1].GetFloat());
            sprite->mutable_content()->set_x_max(content.GetArray()[2].GetFloat());
            sprite->mutable_content()->set_y_max(content.GetArray()[3].GetFloat());
        }
        else
        {
            HRZ_LOG_WARNING(
                "\"content\" property of sprite {} is not an array of 4 numbers", sprite_name);
        }
    }

    auto handle_stretch =
        [&](const char* name, google::protobuf::RepeatedPtrField<hrz_proto::Rangei>* stretches)
    {
        const auto& stretch_array = hrz::json::get_member_or_null(mapbox_sprite, name);
        if (!stretch_array.IsNull())
        {
            if (stretch_array.IsArray())
            {
                for (size_t i = 0; i < stretch_array.Size(); ++i)
                {
                    const auto& stretch = stretch_array.GetArray()[i];
                    if (stretch.IsArray() && stretch.Size() == 2)
                    {
                        auto* range = stretches->Add();
                        range->set_offset(stretch[0].GetFloat());
                        range->set_size(stretch[1].GetFloat() - stretch[0].GetFloat());
                    }
                    else
                    {
                        HRZ_LOG_WARNING(
                            "Component of \"{}\" property of sprite {} is not an array of 2 "
                            "numbers",
                            name, sprite_name);
                    }
                }
            }
            else
            {
                HRZ_LOG_WARNING("\"{}\" property of sprite {} is not an array", name, sprite_name);
            }
        }
    };

    handle_stretch("stretchX", sprite->mutable_stretch_x());
    handle_stretch("stretchY", sprite->mutable_stretch_y());

    return true;
}

bool create_fill_extrusion_repr(
    const rapidjson::Value& paint_node,
    hrz_proto::ExtrudedGeometryVectorRepr* extruded,
    ExpressionContext& expr,
    PropertyAdder& properties)
{
    extruded->set_clip_id(-1);
    extruded->mutable_lighting()->set_enable_lighting(true);
    extruded->mutable_lighting()->set_cast_shadows(true);
    extruded->mutable_lighting()->set_receive_shadows(true);

    // Parse properties.

    // https://docs.mapbox.com/style-spec/reference/layers/#paint-fill-extrusion-fill-extrusion-color
    Property::Generic fill_extrusion_color(Value::from((uint64_t)0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-fill-extrusion-fill-extrusion-opacity
    Property::Generic fill_extrusion_opacity(Value::from(1.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-fill-extrusion-fill-extrusion-vertical-gradient
    Property::Generic fill_extrusion_vertical_gradient(Value::from(true));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-fill-extrusion-fill-extrusion-height
    Property::Generic fill_extrusion_height(Value::from(0.0));

    if (!paint_node.IsNull())
    {
        parse_generic_property(
            "fill-extrusion-color", MapboxPropertyType::Color, paint_node, &fill_extrusion_color,
            expr);
        parse_generic_property(
            "fill-extrusion-opacity", MapboxPropertyType::Number, paint_node,
            &fill_extrusion_opacity, expr);
        parse_generic_property(
            "fill-extrusion-vertical-gradient", MapboxPropertyType::Bool, paint_node,
            &fill_extrusion_vertical_gradient, expr);
        parse_generic_property(
            "fill-extrusion-height", MapboxPropertyType::Number, paint_node, &fill_extrusion_height,
            expr);
    }

    properties.add_extruded_vector_color(
        "extruded_color",
        Property::ExtrudedVectorColor(
            fill_extrusion_color, fill_extrusion_opacity, fill_extrusion_vertical_gradient),
        extruded->mutable_roof_color(), extruded->mutable_upper_color(),
        extruded->mutable_lower_color());

    properties.add("extruded_height", fill_extrusion_height, extruded->mutable_extrusion());

    return true;
}

bool create_fill_repr(
    const rapidjson::Value& paint_node,
    const rapidjson::Value& layout_node,
    const rapidjson::Value& sprite_index,
    std::string_view sprite_image_url,
    hrz_proto::FlatOverlayVectorRepr* flat_overlay,
    std::optional<hrz_proto::FlatOverlayVectorRepr>* outline_flat_overlay,
    uint32_t* next_flat_overlay_z_index,
    ExpressionContext& expr,
    PropertyAdder& properties)
{
    auto initialize_flat_overlay_repr = [&](hrz_proto::FlatOverlayVectorRepr* repr)
    {
        repr->set_z_index(*next_flat_overlay_z_index);
        *next_flat_overlay_z_index += 1;
        repr->set_clip_to_tile(true);
    };

    initialize_flat_overlay_repr(flat_overlay);

    // https://docs.mapbox.com/style-spec/reference/layers/#paint-fill-fill-antialias
    Value fill_antialias(Value::from(true));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-fill-fill-color
    Property::Generic fill_color(Value::from((uint64_t)0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-fill-fill-opacity
    Property::Generic fill_opacity(Value::from(1.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-fill-fill-outline-color
    std::optional<Property::Generic> fill_outline_color_opt;
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-fill-fill-pattern
    Property::Generic fill_pattern(Value::from(""));

    if (!paint_node.IsNull())
    {
        parse_literal_property(
            "fill-antialias", MapboxPropertyType::Bool, paint_node, &fill_antialias, expr);
        parse_generic_property(
            "fill-color", MapboxPropertyType::Color, paint_node, &fill_color, expr);
        parse_generic_property(
            "fill-opacity", MapboxPropertyType::Number, paint_node, &fill_opacity, expr);
        if (!hrz::json::get_member_or_null(paint_node, "fill-outline-color").IsNull())
        {
            // The spec says "Matches the value of fill-color if unspecified".
            // But because outlines are drawn on the inside, against a fill colour
            // that is the same, this is equivalent to not drawing outlines.
            fill_outline_color_opt.emplace(Value::from((uint64_t)0));
            parse_generic_property(
                "fill-outline-color", MapboxPropertyType::Color, paint_node,
                &fill_outline_color_opt.value(), expr);
        }
        parse_generic_property(
            "fill-pattern", MapboxPropertyType::String, paint_node, &fill_pattern, expr);
    }

    bool has_fill_pattern = !fill_pattern.is_literal() || !fill_pattern.default_value.str.empty();

    // The spec says about fill-outline-color "Disabled by fill-pattern".
    // But in practice it isn't true.
    bool has_outline = fill_antialias.b64 == true && fill_outline_color_opt.has_value()
        && (!fill_opacity.is_literal() || fill_opacity.default_value.f64 > 0.0);

    if (!has_fill_pattern)
    {
        properties.add(
            "flat_overlay_color", Property::ColorWithOpacity(fill_color, fill_opacity),
            flat_overlay->mutable_color());
    }
    else
    {
        flat_overlay->mutable_color()->mutable_default_value()->set_r(1.0f);
        flat_overlay->mutable_color()->mutable_default_value()->set_g(1.0f);
        flat_overlay->mutable_color()->mutable_default_value()->set_b(1.0f);
        flat_overlay->mutable_color()->mutable_default_value()->set_a(0.0f);
    }

    if (has_outline)
    {
        outline_flat_overlay->emplace();
        initialize_flat_overlay_repr(&outline_flat_overlay->value());

        outline_flat_overlay->value().set_polygons_outline(true);
        outline_flat_overlay->value().set_line_width_unit(
            hrz_proto::InWorldSizeUnit::IN_WORLD_SIZE_IN_PIXELS);
        outline_flat_overlay->value().mutable_line_width()->set_default_value(1.0f);
        outline_flat_overlay->value().set_side(hrz_proto::PolylineSide::SIDE_INSIDE);

        properties.add(
            "flat_overlay_outline_color",
            Property::ColorWithOpacity(fill_outline_color_opt.value(), fill_opacity),
            outline_flat_overlay->value().mutable_color());
    }

    if (has_fill_pattern)
    {
        hrz_proto::ImageSymbolElement image;

        if (sprite_index.IsObject())
        {
            for (auto it = sprite_index.MemberBegin(); it != sprite_index.MemberEnd(); ++it)
            {
                parse_mapbox_sprite(it->name.GetString(), it->value, &image);
            }
        }

        flat_overlay->set_pattern_image_url(sprite_image_url.data(), sprite_image_url.size());
        flat_overlay->mutable_pattern_image_http_headers()->CopyFrom(image.http_headers());

        for (const auto& sprite : image.sprites())
        {
            flat_overlay->add_pattern_sprites()->CopyFrom(sprite);
        }

        properties.add(
            "polygon_pattern_sprite_name", fill_pattern,
            flat_overlay->mutable_polygon_pattern_sprite_name());
        flat_overlay->mutable_polygon_pattern_size()->mutable_default_value()->set_x(1.0f);
        flat_overlay->mutable_polygon_pattern_size()->mutable_default_value()->set_y(1.0f);
        flat_overlay->set_polygon_pattern_size_unit(
            hrz_proto::PolygonPatternSizeUnit::POLYGON_PATTERN_SIZE_RELATIVE_TO_SPRITE_IN_PIXELS);

        Property::Generic white(Value::from((uint64_t)0xffffffff));
        properties.add(
            "polygon_pattern_color", Property::ColorWithOpacity(white, fill_opacity),
            flat_overlay->mutable_polygon_pattern_color());
    }

    return true;
}

bool create_line_repr(
    const rapidjson::Value& paint_node,
    const rapidjson::Value& layout_node,
    hrz_proto::FlatOverlayVectorRepr* flat_overlay,
    uint32_t* next_flat_overlay_z_index,
    ExpressionContext& expr,
    PropertyAdder& properties)
{
    flat_overlay->set_line_width_unit(hrz_proto::InWorldSizeUnit::IN_WORLD_SIZE_IN_PIXELS);
    flat_overlay->set_dash_length_unit(hrz_proto::DashSizeUnit::DASH_SIZE_IN_PIXELS);
    flat_overlay->set_dash_period_unit(hrz_proto::DashSizeUnit::DASH_SIZE_IN_PIXELS);
    flat_overlay->set_z_index((*next_flat_overlay_z_index)++);
    flat_overlay->set_polygons_outline(true);
    flat_overlay->set_clip_to_tile(true);

    // https://docs.mapbox.com/style-spec/reference/layers/#paint-line-line-color
    Property::Generic line_color(Value::from((uint64_t)0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-line-line-opacity
    Property::Generic line_opacity(Value::from(1.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-line-line-width
    Property::Generic line_width(Value::from(1.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-line-line-dasharray
    Property::Generic line_dasharray({});
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-line-line-cap
    Value line_cap(Value::from("butt"));

    if (!paint_node.IsNull())
    {
        parse_generic_property(
            "line-color", MapboxPropertyType::Color, paint_node, &line_color, expr);
        parse_generic_property(
            "line-opacity", MapboxPropertyType::Number, paint_node, &line_opacity, expr);
        parse_generic_property(
            "line-width", MapboxPropertyType::Number, paint_node, &line_width, expr);
        parse_generic_property(
            "line-dasharray", MapboxPropertyType::NumberArray, paint_node, &line_dasharray, expr);
    }

    if (!layout_node.IsNull())
    {
        parse_literal_property(
            "line-cap", MapboxPropertyType::String, layout_node, &line_cap, expr);
    }

    // Extract dash length and period from line-dasharray.
    Property::Generic dash_length(Value::from(0.0));
    Property::Generic dash_period(Value::from(0.0));

    if (line_dasharray.node != NO_NODE)
    {
        if (expr.nodes.at(line_dasharray.node).children.size() >= 2)
        {
            unwrap_array_properties(line_dasharray, {&dash_length, &dash_period}, expr);

            assert(dash_length.is_literal());
            assert(dash_period.is_literal());

            // Mapbox dasharray alternates between dash length and gap length.
            // Horizon only lets the user specify one dash length and dash period (which is the
            // distance between the start of two consecutive dashes, so we have to add the length
            // of the dash and the length of the gap to obtain the correct value).

            // @Todo(HRZ-1047) For a specified dash length of L, Mapbox displays effective
            // lengths in the [L ; 2 * L] range, while Horizon would currently do it in
            // the [0.5 * L ; L] range, so to match the Mapbox behavior, we also multiply the
            // parsed values by 2.
            static constexpr double mapbox_to_horizon_scale_factor = 2.0;

            // Values in the dasharray are specified in "line widths".
            // To convert them in to pixels, we have to multiply by the line width.
            if (line_width.is_literal())
            {
                dash_length.default_value.f64 = dash_length.default_value.f64
                    * line_width.default_value.f64 * mapbox_to_horizon_scale_factor;
                dash_period.default_value.f64 =
                    (dash_period.default_value.f64 * line_width.default_value.f64
                     * mapbox_to_horizon_scale_factor)
                    + dash_length.default_value.f64;
            }
            else
            {
                auto factor_node = expr.add_node(
                    Node::Type::Multiply,
                    {line_width.node,
                     expr.add_literal(Value::from(mapbox_to_horizon_scale_factor))});
                dash_length.node =
                    expr.add_node(Node::Type::Multiply, {dash_length.node, factor_node});
                dash_period.node =
                    expr.add_node(Node::Type::Multiply, {dash_period.node, factor_node});
                dash_period.node =
                    expr.add_node(Node::Type::Add, {dash_period.node, dash_length.node});
            }
        }
    }

    properties.add(
        "line_color", Property::ColorWithOpacity(line_color, line_opacity),
        flat_overlay->mutable_color());

    properties.add("line_width", line_width, flat_overlay->mutable_line_width());

    properties.add("dash_length", dash_length, flat_overlay->mutable_dash_length());

    properties.add("dash_period", dash_period, flat_overlay->mutable_dash_period());

    if (dash_length.is_literal() && dash_length.default_value.f64 == 0.0)
    {
        flat_overlay->set_dash_mode(hrz_proto::DashMode::DASH_DISABLED);
    }
    else
    {
        flat_overlay->set_dash_mode(hrz_proto::DashMode::DASH_ENABLED_FILLED);
    }

    flat_overlay->set_round_tips(line_cap.str == std::string_view("round"));

    return true;
}

bool create_circle_repr(
    const rapidjson::Value& paint_node,
    const rapidjson::Value& layout_node,
    hrz_proto::SymbolVectorRepr* symbol,
    uint32_t* next_symbol_z_index,
    ExpressionContext& expr,
    PropertyAdder& properties)
{
    symbol->set_clip_to_tile(true);
    symbol->set_z_index((*next_symbol_z_index)++);

    // We can't control which layer occludes the symbol like Mapbox does.
    // The most desirable option in that case seems to let the symbols overlap all the other
    // (non-symbol) layers, as it is most often the case in 2D maps.
    symbol->set_ignore_world_occlusions(true);

    symbol->mutable_root_element()->set_type(hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
    auto* anchor = symbol->mutable_root_element()->mutable_anchor();
    anchor->set_can_overlap_other_symbols(true);

    anchor->mutable_child()->set_type(hrz_proto::SymbolElementType::SIZED_BOX_SYMBOL_ELEMENT);
    auto* sized_box = anchor->mutable_child()->mutable_sized_box();

    sized_box->mutable_child()->set_type(
        hrz_proto::SymbolElementType::DECORATED_SHAPE_SYMBOL_ELEMENT);
    auto* circle = sized_box->mutable_child()->mutable_decorated_shape();
    circle->set_shape_type(hrz_proto::DecoratedShapeType::DECORATED_SHAPE_CIRCLE);

    // https://docs.mapbox.com/style-spec/reference/layers/#paint-circle-circle-color
    Property::Generic circle_color(Value::from((uint64_t)0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-circle-circle-opacity
    Property::Generic circle_opacity(Value::from(1.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-circle-circle-pitch-alignment
    Value circle_pitch_alignment(Value::from("viewport"));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-circle-circle-radius
    Property::Generic circle_radius(Value::from(5.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-circle-circle-stroke-color
    Property::Generic circle_stroke_color(Value::from((uint64_t)0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-circle-circle-stroke-opacity
    Property::Generic circle_stroke_opacity(Value::from(1.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-circle-circle-stroke-width
    Property::Generic circle_stroke_width(Value::from(0.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-circle-circle-stroke-translate
    Property::Generic circle_translate({});
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-circle-circle-stroke-translate-anchor
    Value circle_translate_anchor(Value::from("map"));

    if (!paint_node.IsNull())
    {
        parse_generic_property(
            "circle-color", MapboxPropertyType::Color, paint_node, &circle_color, expr);
        parse_literal_property(
            "circle-pitch-alignment", MapboxPropertyType::String, paint_node,
            &circle_pitch_alignment, expr);
        parse_generic_property(
            "circle-opacity", MapboxPropertyType::Number, paint_node, &circle_opacity, expr);
        parse_generic_property(
            "circle-radius", MapboxPropertyType::Number, paint_node, &circle_radius, expr);
        parse_generic_property(
            "circle-stroke-color", MapboxPropertyType::Color, paint_node, &circle_stroke_color,
            expr);
        parse_generic_property(
            "circle-stroke-opacity", MapboxPropertyType::Number, paint_node, &circle_stroke_opacity,
            expr);
        parse_generic_property(
            "circle-stroke-width", MapboxPropertyType::Number, paint_node, &circle_stroke_width,
            expr);
        parse_generic_property(
            "circle-translate", MapboxPropertyType::NumberArray, paint_node, &circle_translate,
            expr);
        parse_literal_property(
            "circle-translate-anchor", MapboxPropertyType::String, paint_node,
            &circle_translate_anchor, expr);
    }

    // Extract circle-translate components
    Property::Generic circle_translate_x(Value::from(0.0));
    Property::Generic circle_translate_y(Value::from(0.0));
    if (circle_translate.node != NO_NODE)
    {
        unwrap_array_properties(circle_translate, {&circle_translate_x, &circle_translate_y}, expr);
    }

    // In Mapbox, the stroke is added to the total radius of the circle, whereas it is taken
    // from the total radius in Horizon.
    // The total radius must also be multiplied by 2, since the size of the symbol is dictated
    // by a property the size of a box, not the radius of a circle.
    Property::Generic sized_box_size(Value::from(0.0));

    if (circle_radius.is_literal() && circle_stroke_width.is_literal())
    {
        // We can directly edit the default value of the radius.
        sized_box_size.default_value.f64 =
            2.0 * (circle_radius.default_value.f64 + circle_stroke_width.default_value.f64);
    }
    else
    {
        // We have to add both values through the styling script.
        NodeIndex radius_node = get_or_create_node(circle_radius, expr);
        NodeIndex stroke_node = get_or_create_node(circle_stroke_width, expr);

        NodeIndex total_radius_node = expr.add_node(Node::Type::Add, {radius_node, stroke_node});
        NodeIndex factor_node = expr.add_literal(Value::from(2.0));

        sized_box_size.node = expr.add_node(Node::Type::Multiply, {total_radius_node, factor_node});
    }

    Property::Generic position_offset_x(Value::from(0.0));
    Property::Generic position_offset_y(Value::from(0.0));
    Property::Generic position_offset_z(Value::from(0.0));

    if (circle_pitch_alignment.str == "map")
    {
        // Rotate the symbol to have it face the sky like in Mapbox GL
        anchor->mutable_rotation()->mutable_default_value()->set_x(lm::PI / 2.0);
        anchor->set_y_axis_alignment(hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_WORLD);

        // Offset by the tiniest amount to avoid Z-fighting with flat terrain
        position_offset_z.default_value.f64 = 0.1;
    }
    else // "viewport"
    {
        anchor->set_y_axis_alignment(hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_SCREEN);
    }

    properties.add(
        "circle_color", Property::ColorWithOpacity(circle_color, circle_opacity),
        circle->mutable_color());

    properties.add(
        "circle_border_color",
        Property::ColorWithOpacity(circle_stroke_color, circle_stroke_opacity),
        circle->mutable_border_color());

    properties.add("circle_border_size", circle_stroke_width, circle->mutable_border_size());

    properties.add(
        "circle_box_size", Property::Vec2(sized_box_size, sized_box_size),
        sized_box->mutable_size());

    if (circle_translate_anchor.str == "viewport")
    {
        // Anchors don't have the ability to specify the axes of its position offset.
        // However, we can use a Transform component to translate the symbol relatively to the
        // viewport.
        hrz_proto::SymbolElement original_anchor_child(anchor->child());

        anchor->mutable_child()->set_type(hrz_proto::SymbolElementType::TRANSFORM_SYMBOL_ELEMENT);
        auto* transform = anchor->mutable_child()->mutable_transform();

        transform->mutable_child()->CopyFrom(original_anchor_child);
        sized_box = transform->mutable_child()->mutable_sized_box();

        auto* transform_component = transform->add_components();
        transform_component->set_type(
            hrz_proto::TransformSymbolComponentType::TRANSFORM_SYMBOL_TRANSLATION);

        properties.add(
            "transform_translation", Property::Vec2(circle_translate_x, circle_translate_y),
            transform_component->mutable_translation());
    }
    else // "map"
    {
        position_offset_x = circle_translate_x;
        position_offset_y = circle_translate_y;

        // With the map anchor, a positive Y translation should offset to the South, and negative
        // values to the North.
        if (position_offset_y.is_literal())
        {
            position_offset_y.default_value.f64 *= -1;
        }
        else
        {
            position_offset_y.node = expr.add_node(Node::Type::Subtract, {position_offset_y.node});
        }
    }

    properties.add(
        "circle_position_offset",
        Property::Vec3(position_offset_x, position_offset_y, position_offset_z),
        anchor->mutable_position_offset());

    return true;
}

bool create_heatmap_repr(
    const rapidjson::Value& paint_node,
    const rapidjson::Value& layout_node,
    hrz_proto::HeatmapVectorRepr* heatmap,
    uint32_t* next_flat_overlay_z_index,
    ExpressionContext& expr,
    PropertyAdder& properties)
{
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-heatmap-heatmap-weight
    Property::Generic heatmap_weight(Value::from(1.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-heatmap-heatmap-radius
    Property::Generic heatmap_radius(Value::from(30.0));

    float opacity = 1.0;

    auto use_default_palette = [&]()
    {
        auto palette = heatmap->mutable_numeric_palette();

        static constexpr size_t stop_count = 6;
        static constexpr float stop_values[stop_count] = {0, 0.1, 0.3, 0.5, 0.7, 1};
        static uint32_t stop_colors[stop_count] = {
            0x00ff0000, // blue
            0xffe16941, // royalblue
            0xffffff00, // cyan
            0xff00ff00, // lime
            0xff00ffff, // yellow
            0xff0000ff, // red
        };

        for (size_t i = 0; i < stop_count; ++i)
        {
            auto stop = palette->add_color_points();
            stop->set_value(stop_values[i]);
            auto proto_color = convert_uint_to_proto_color(stop_colors[i]);
            proto_color.set_a(proto_color.a() * opacity);
            *stop->mutable_first_color() = proto_color;
            *stop->mutable_second_color() = proto_color;
        }
    };

    if (!paint_node.IsNull())
    {
        // https://docs.mapbox.com/style-spec/reference/layers/#paint-heatmap-heatmap-intensity
        NodeIndex intensity_expr_root_index = NO_NODE;
        auto& intensity_node = json::get_member_or_null(paint_node, "heatmap-intensity");
        if (!intensity_node.IsNull())
        {
            intensity_expr_root_index =
                parse_expression(intensity_node, MapboxPropertyType::Number, expr);
        }

        NodeIndex weight_expr_root_index = parse_expression(
            json::get_member_or_null(paint_node, "heatmap-weight"), MapboxPropertyType::Number,
            expr);
        if (weight_expr_root_index == NO_NODE)
        {
            weight_expr_root_index = expr.add_literal(Value::from(1.0));
        }

        if (intensity_expr_root_index != NO_NODE)
        {
            heatmap_weight.node = expr.add_node(
                Node::Type::Multiply, {intensity_expr_root_index, weight_expr_root_index});
        }
        else
        {
            heatmap_weight.node = weight_expr_root_index;
        }

        {
            // https://docs.mapbox.com/style-spec/reference/layers/#paint-heatmap-heatmap-opacity
            ExpressionContext heatmap_opacity_expr(expr.mapbox_layer_id, expr.attributes);

            NodeIndex heatmap_opacity_root_index = parse_expression(
                json::get_member_or_null(paint_node, "heatmap-opacity"), MapboxPropertyType::Number,
                heatmap_opacity_expr);

            if (heatmap_opacity_root_index != NO_NODE)
            {
                const auto& node = heatmap_opacity_expr.nodes.at(heatmap_opacity_root_index);
                if (node.type == Node::Type::Literal && node.literal.type == Value::Type::Double)
                {
                    // We have a constant opacity value, that can be applied to the palette.
                    opacity = hrz::clamp((float)node.literal.f64, 0.0f, 1.0f);
                }
            }
        }

        ExpressionContext heatmap_color_expr(expr.mapbox_layer_id, expr.attributes);

        NodeIndex heatmap_color_root_index = parse_expression(
            json::get_member_or_null(paint_node, "heatmap-color"), MapboxPropertyType::Color,
            heatmap_color_expr);

        if (heatmap_color_root_index != NO_NODE)
        {
            if (heatmap_color_expr.palettes.size() == 1
                && heatmap_color_expr.palettes.front().type() == hrz_proto::PaletteType::NUMERIC)
            {
                // We have a usable palette

                heatmap->mutable_numeric_palette()->CopyFrom(
                    heatmap_color_expr.palettes.front().numeric());

                for (auto& stop : *heatmap->mutable_numeric_palette()->mutable_color_points())
                {
                    stop.mutable_first_color()->set_a(stop.first_color().a() * opacity);
                    stop.mutable_second_color()->set_a(stop.second_color().a() * opacity);
                }
            }
            else
            {
                const auto& color_node = heatmap_color_expr.nodes.at(heatmap_color_root_index);
                if (color_node.type == Node::Type::Literal
                    && color_node.literal.type == Value::Type::UInt)
                {
                    // We have a constant colour, define a simple palette with it
                    auto color = (uint32_t)color_node.literal.u64;

                    auto palette = heatmap->mutable_numeric_palette();

                    auto stop = palette->add_color_points();
                    stop->set_value(0);
                    auto proto_color = convert_uint_to_proto_color(color);
                    proto_color.set_a(proto_color.a() * opacity);
                    *stop->mutable_first_color() = proto_color;
                    *stop->mutable_second_color() = proto_color;
                }
                else
                {
                    HRZ_LOG_ERROR("Unable to generate a palette for the heatmap color");
                    return false;
                }
            }
        }
        else
        {
            use_default_palette();
        }

        parse_numeric_property<double>(
            "heatmap-radius", MapboxPropertyType::Number, {0.4f}, {1.0}, std::nullopt, paint_node,
            &heatmap_radius, expr);
    }
    else
    {
        use_default_palette();
    }

    properties.add("heatmap_value", heatmap_weight, heatmap->mutable_value());
    properties.add("heatmap_disc_radius", heatmap_radius, heatmap->mutable_disc_radius());

    heatmap->set_disc_radius_size_unit(hrz_proto::InWorldSizeUnit::IN_WORLD_SIZE_IN_PIXELS);
    heatmap->set_z_index((*next_flat_overlay_z_index)++);
    heatmap->set_blur_size(1.2f);

    return true;
}

void handle_common_layout_properties(
    const Value& allow_overlap,
    const Property::Generic& anchor,
    const Value& ignore_placement,
    const Value& keep_upright,
    const Property::Generic& offset,
    const Value& optional,
    const Property::Generic& padding,
    Value& pitch_alignment,
    const Property::Generic& rotate,
    Value& rotation_alignment,
    const Property::Generic& translate,
    const Value& translate_anchor,
    const Value& symbol_placement,
    const Property::Generic& symbol_sort_key,
    const char* anchor_alignment_prp_name,
    const char* padding_left_prp_name,
    const char* padding_top_prp_name,
    const char* padding_right_prp_name,
    const char* padding_bottom_prp_name,
    const char* translate_prp_name,
    const char* position_offset_prp_name,
    const char* rotation_prp_name,
    const char* culling_priority_prp_name,
    std::string* layer_anchor_angle_attribute_name,
    hrz_proto::AnchorSymbolElement* anchor_element,
    hrz_proto::PaddingSymbolElement* padding_element,
    hrz_proto::TransformSymbolElement* transform_element,
    ExpressionContext& expr,
    PropertyAdder& properties)
{
    anchor_element->set_can_overlap_other_symbols(allow_overlap.b64);
    anchor_element->set_hides_other_symbols(!ignore_placement.b64);
    anchor_element->set_is_optional(optional.b64);
    anchor_element->set_rotation_order(hrz_proto::EulerRotationOrder::EULER_XYZ);

    Property::Generic translate_x(Value::from(0.0));
    Property::Generic translate_y(Value::from(0.0));
    if (translate.node != NO_NODE)
    {
        unwrap_array_properties(translate, {&translate_x, &translate_y}, expr);
    }

    Property::Generic offset_x(Value::from(0.0));
    Property::Generic offset_y(Value::from(0.0));
    if (offset.node != NO_NODE)
    {
        unwrap_array_properties(offset, {&offset_x, &offset_y}, expr);
    }

    properties.add(
        anchor_alignment_prp_name, Property::SymbolAnchorAlignment(anchor),
        anchor_element->mutable_element_alignment());

    Property::Generic padding_left(Value::from(0.0));
    Property::Generic padding_top(Value::from(0.0));
    Property::Generic padding_right(Value::from(0.0));
    Property::Generic padding_bottom(Value::from(0.0));

    if (offset_x.is_literal() && offset_y.is_literal() && padding.is_literal())
    {
        padding_left.default_value.f64 = offset_x.default_value.f64 + padding.default_value.f64;
        padding_top.default_value.f64 = offset_y.default_value.f64 + padding.default_value.f64;
        padding_right.default_value.f64 = padding.default_value.f64;
        padding_bottom.default_value.f64 = padding.default_value.f64;
    }
    else
    {
        auto offset_x_node = get_or_create_node(offset_x, expr);
        auto offset_y_node = get_or_create_node(offset_y, expr);
        auto padding_node = get_or_create_node(padding, expr);

        padding_left.node = expr.add_node(Node::Type::Add, {offset_x_node, padding_node});
        padding_top.node = expr.add_node(Node::Type::Add, {offset_y_node, padding_node});
        padding_right.node = padding_node;
        padding_bottom.node = padding_node;
    }

    properties.add(padding_left_prp_name, padding_left, padding_element->mutable_left_padding());
    properties.add(padding_top_prp_name, padding_top, padding_element->mutable_top_padding());
    properties.add(padding_right_prp_name, padding_right, padding_element->mutable_right_padding());
    properties.add(
        padding_bottom_prp_name, padding_bottom, padding_element->mutable_bottom_padding());

    Property::Generic position_offset_z(Value::from(0.0));
    Property::Generic rotation_x(Value::from(0.0));
    Property::Generic rotation_y = rotate;

    // Mapbox specifies rotations in degrees: convert them to radians.
    if (rotation_y.is_literal())
    {
        rotation_y.default_value.f64 = lm::radians(rotation_y.default_value.f64);
    }
    else
    {
        auto factor = expr.add_literal(Value::from(lm::PI / 180.0));
        rotation_y.node = expr.add_node(Node::Type::Multiply, {rotation_y.node, factor});
    }

    if (rotation_alignment.str == "auto")
    {
        if (symbol_placement.str == "point")
        {
            rotation_alignment.str = "viewport";
        }
        else
        {
            rotation_alignment.str = "map";
        }
    }

    if (rotation_alignment.str == "viewport")
    {
        anchor_element->set_x_axis_alignment(hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_SCREEN);
    }
    else // "map"
    {
        anchor_element->set_x_axis_alignment(hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_WORLD);
        if (symbol_placement.str == "line" || symbol_placement.str == "line-center")
        {
            if (layer_anchor_angle_attribute_name->empty())
            {
                *layer_anchor_angle_attribute_name = "anchor_angle";
            }

            auto anchor_angle_name =
                expr.add_literal(Value::from(*layer_anchor_angle_attribute_name));
            auto anchor_angle = expr.add_node(Node::Type::Attribute, {anchor_angle_name});

            if (rotation_y.is_literal() && rotation_y.default_value.f64 == 0.0)
            {
                rotation_y.node = anchor_angle;
            }
            else
            {
                auto angle_offset = rotation_y.node;
                if (angle_offset == NO_NODE)
                {
                    assert(rotation_y.default_value.type == Value::Type::Double);
                    angle_offset = expr.add_literal(rotation_y.default_value);
                }

                rotation_y.node = expr.add_node(Node::Type::Add, {anchor_angle, angle_offset});
            }

            if (keep_upright.b64 == true)
            {
                anchor_element->set_keep_upright(true);
            }
        }
    }

    if (pitch_alignment.str == "auto")
    {
        assert(rotation_alignment.str != "auto");
        pitch_alignment.str = rotation_alignment.str;
    }

    if (pitch_alignment.str == "viewport")
    {
        anchor_element->set_y_axis_alignment(hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_SCREEN);
    }
    else // "map"
    {
        // Rotate the symbol to have it face the sky like in Mapbox GL
        rotation_x.default_value.f64 = lm::PI / 2.0;
        anchor_element->set_y_axis_alignment(hrz_proto::SymbolAxisAlignment::AXIS_ALIGNMENT_WORLD);

        // Offset by the tiniest amount to avoid Z-fighting with flat terrain
        position_offset_z.default_value.f64 = 0.1;
    }

    Property::Generic position_offset_x(Value::from(0.0));
    Property::Generic position_offset_y(Value::from(0.0));

    if (translate_anchor.str == "viewport")
    {
        auto* transform_component = transform_element->add_components();
        transform_component->set_type(
            hrz_proto::TransformSymbolComponentType::TRANSFORM_SYMBOL_TRANSLATION);

        properties.add(
            translate_prp_name, Property::Vec3(translate_x, translate_y, Value::from(0.0)),
            transform_component->mutable_translation());
    }
    else // "map"
    {
        position_offset_x = translate_x;
        position_offset_y = translate_y;
    }

    properties.add(
        position_offset_prp_name,
        Property::Vec3(position_offset_x, position_offset_y, position_offset_z),
        anchor_element->mutable_position_offset());

    properties.add(
        rotation_prp_name,
        Property::Vec3(rotation_x, rotation_y, Property::Generic(Value::from(0.0))),
        anchor_element->mutable_rotation());

    Property::Generic culling_priority(symbol_sort_key);

    // The sort key is the order in which symbols are drawn. So:
    // * Lower sort keys appear under higher sort keys
    // * Lower sort keys have culling priority over higher sort keys
    //
    // Which is why we multiply the sort key by -1.
    if (culling_priority.is_literal())
    {
        culling_priority.default_value.f64 *= -1;
    }
    else
    {
        culling_priority.node = expr.add_node(Node::Type::Subtract, {culling_priority.node});
    }

    properties.add(
        culling_priority_prp_name, culling_priority, anchor_element->mutable_culling_priority());
}

bool create_symbol_repr(
    const rapidjson::Value& paint_node,
    const rapidjson::Value& layout_node,
    const rapidjson::Value& sprite_index,
    std::string_view sprite_image_url,
    const hrz::flat_hash_map<std::string, std::string>& font_family_urls,
    hrz_proto::SymbolVectorRepr* symbol,
    std::string* anchor_angle_attribute_name,
    uint32_t* next_symbol_z_index,
    ExpressionContext& expr,
    PropertyAdder& properties)
{
    symbol->set_clip_to_tile(true);
    symbol->set_z_index((*next_symbol_z_index)++);

    // We can't control which layer occludes the symbol like Mapbox does.
    // The most desirable option in that case seems to let the symbols overlap all the other
    // (non-symbol) layers, as it is most often the case in 2D maps.
    symbol->set_ignore_world_occlusions(true);

    symbol->mutable_root_element()->set_type(hrz_proto::SymbolElementType::STACK_SYMBOL_ELEMENT);
    auto* stack = symbol->mutable_root_element()->mutable_stack();

    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-allow-overlap
    Value icon_allow_overlap(Value::from(false));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-anchor
    Property::Generic icon_anchor(Value::from("center"));
    // Note: "icon-color" is reserved for SDF icons.
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-ignore-placement
    Value icon_ignore_placement(Value::from(false));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-image
    Property::Generic icon_image(Value::from(""));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-keep-upright
    Value icon_keep_upright(Value::from(false));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-offset
    Property::Generic icon_offset(Value::from(false));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-symbol-icon-opacity
    Property::Generic icon_opacity(Value::from(1.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-optional
    Value icon_optional(Value::from(false));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-padding
    Property::Generic icon_padding(Value::from(2.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-pitch-alignment
    Value icon_pitch_alignment(Value::from("auto"));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-rotate
    Property::Generic icon_rotate(Value::from(0.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-rotation-alignment
    Value icon_rotation_alignment(Value::from("auto"));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-size
    Property::Generic icon_size(Value::from(1.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-text-fit
    Value icon_text_fit(Value::from("none"));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-icon-text-fit-padding
    Property::Generic icon_text_fit_padding({});
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-symbol-icon-translate
    Property::Generic icon_translate({});
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-symbol-icon-translate-anchor
    Value icon_translate_anchor(Value::from("map"));

    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-symbol-placement
    Value symbol_placement(Value::from("point"));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-symbol-sort-key
    Property::Generic symbol_sort_key(Value::from(0.0));

    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-allow-overlap
    Value text_allow_overlap(Value::from(false));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-anchor
    Property::Generic text_anchor(Value::from("center"));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-symbol-text-color
    Property::Generic text_color(Value::from((uint64_t)0));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-field
    Property::Generic text_field(Value::from(""));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-font
    Property::Generic text_font({});
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-symbol-text-halo-color
    Property::Generic text_halo_color(Value::from((uint64_t)0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-symbol-text-halo-width
    Property::Generic text_halo_width(Value::from(0.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-ignore-placement
    Value text_ignore_placement(Value::from(false));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-justify
    Property::Generic text_justify(Value::from("center"));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-keep-upright
    Value text_keep_upright(Value::from(true));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-line-height
    Property::Generic text_line_height(Value::from(1.2));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-offset
    Property::Generic text_offset(Value::from(false));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-symbol-text-opacity
    Property::Generic text_opacity(Value::from(1.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-optional
    Value text_optional(Value::from(false));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-padding
    Property::Generic text_padding(Value::from(2.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-pitch-alignment
    Value text_pitch_alignment(Value::from("auto"));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-rotate
    Property::Generic text_rotate(Value::from(0.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-rotation-alignment
    Value text_rotation_alignment(Value::from("auto"));
    // https://docs.mapbox.com/style-spec/reference/layers/#layout-symbol-text-size
    Property::Generic text_size(Value::from(16.0));
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-symbol-text-translate
    Property::Generic text_translate({});
    // https://docs.mapbox.com/style-spec/reference/layers/#paint-symbol-text-translate-anchor
    Value text_translate_anchor(Value::from("map"));

    if (!paint_node.IsNull())
    {
        parse_generic_property(
            "icon-opacity", MapboxPropertyType::Number, paint_node, &icon_opacity, expr);
        parse_generic_property(
            "icon-translate", MapboxPropertyType::NumberArray, paint_node, &icon_translate, expr);
        parse_literal_property(
            "icon-translate-anchor", MapboxPropertyType::String, paint_node, &icon_translate_anchor,
            expr);

        parse_generic_property(
            "text-color", MapboxPropertyType::Color, paint_node, &text_color, expr);
        parse_generic_property(
            "text-halo-color", MapboxPropertyType::Color, paint_node, &text_halo_color, expr);
        parse_generic_property(
            "text-halo-width", MapboxPropertyType::Number, paint_node, &text_halo_width, expr);
        parse_generic_property(
            "text-opacity", MapboxPropertyType::Number, paint_node, &text_opacity, expr);
        parse_generic_property(
            "text-translate", MapboxPropertyType::NumberArray, paint_node, &text_translate, expr);
        parse_literal_property(
            "text-translate-anchor", MapboxPropertyType::String, paint_node, &text_translate_anchor,
            expr);
    }

    if (!layout_node.IsNull())
    {
        parse_literal_property(
            "icon-allow-overlap", MapboxPropertyType::Bool, layout_node, &icon_allow_overlap, expr);
        parse_generic_property(
            "icon-anchor", MapboxPropertyType::String, layout_node, &icon_anchor, expr);
        parse_literal_property(
            "icon-ignore-placement", MapboxPropertyType::Bool, layout_node, &icon_ignore_placement,
            expr);
        parse_generic_property(
            "icon-image", MapboxPropertyType::String, layout_node, &icon_image, expr);
        parse_literal_property(
            "icon-keep-upright", MapboxPropertyType::Bool, layout_node, &icon_keep_upright, expr);
        parse_generic_property(
            "icon-offset", MapboxPropertyType::NumberArray, layout_node, &icon_offset, expr);
        parse_literal_property(
            "icon-optional", MapboxPropertyType::Bool, layout_node, &icon_optional, expr);
        parse_generic_property(
            "icon-padding", MapboxPropertyType::Number, layout_node, &icon_padding, expr);
        parse_literal_property(
            "icon-pitch-alignment", MapboxPropertyType::String, layout_node, &icon_pitch_alignment,
            expr);
        parse_generic_property(
            "icon-rotate", MapboxPropertyType::Number, layout_node, &icon_rotate, expr);
        parse_literal_property(
            "icon-rotation-alignment", MapboxPropertyType::String, layout_node,
            &icon_rotation_alignment, expr);
        parse_generic_property(
            "icon-size", MapboxPropertyType::Number, layout_node, &icon_size, expr);
        parse_literal_property(
            "icon-text-fit", MapboxPropertyType::String, layout_node, &icon_text_fit, expr);
        parse_generic_property(
            "icon-text-fit-padding", MapboxPropertyType::NumberArray, layout_node,
            &icon_text_fit_padding, expr);

        parse_literal_property(
            "symbol-placement", MapboxPropertyType::String, layout_node, &symbol_placement, expr);
        parse_generic_property(
            "symbol-sort-key", MapboxPropertyType::Number, layout_node, &symbol_sort_key, expr);

        parse_literal_property(
            "text-allow-overlap", MapboxPropertyType::Bool, layout_node, &text_allow_overlap, expr);
        parse_generic_property(
            "text-anchor", MapboxPropertyType::String, layout_node, &text_anchor, expr);

        parse_to_string_property(
            "text-field", MapboxPropertyType::FormattedString, layout_node, &text_field, expr);

        parse_generic_property(
            "text-font", MapboxPropertyType::StringArray, layout_node, &text_font, expr);
        parse_literal_property(
            "text-ignore-placement", MapboxPropertyType::Bool, layout_node, &text_ignore_placement,
            expr);
        parse_generic_property(
            "text-justify", MapboxPropertyType::String, layout_node, &text_justify, expr);
        parse_literal_property(
            "text-keep-upright", MapboxPropertyType::Bool, layout_node, &text_keep_upright, expr);
        parse_generic_property(
            "text-line-height", MapboxPropertyType::Number, layout_node, &text_line_height, expr);
        parse_generic_property(
            "text-offset", MapboxPropertyType::NumberArray, layout_node, &text_offset, expr);
        parse_literal_property(
            "text-optional", MapboxPropertyType::Bool, layout_node, &text_optional, expr);
        parse_generic_property(
            "text-padding", MapboxPropertyType::Number, layout_node, &text_padding, expr);
        parse_literal_property(
            "text-pitch-alignment", MapboxPropertyType::String, layout_node, &text_pitch_alignment,
            expr);
        parse_generic_property(
            "text-rotate", MapboxPropertyType::Number, layout_node, &text_rotate, expr);
        parse_literal_property(
            "text-rotation-alignment", MapboxPropertyType::String, layout_node,
            &text_rotation_alignment, expr);
        parse_generic_property(
            "text-size", MapboxPropertyType::Number, layout_node, &text_size, expr);
    }

    bool has_icon = !icon_image.is_literal() || !icon_image.default_value.str.empty();
    bool has_text = !text_field.is_literal() || !text_field.default_value.str.empty();

    if (has_icon)
    {
        auto* child = stack->add_children();
        child->set_type(hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);

        auto* anchor = child->mutable_anchor();
        anchor->mutable_child()->set_type(hrz_proto::SymbolElementType::PADDING_SYMBOL_ELEMENT);

        auto* padding = anchor->mutable_child()->mutable_padding();
        padding->mutable_child()->set_type(hrz_proto::SymbolElementType::TRANSFORM_SYMBOL_ELEMENT);

        auto* transform = padding->mutable_child()->mutable_transform();
        transform->mutable_child()->set_type(hrz_proto::SymbolElementType::IMAGE_SYMBOL_ELEMENT);

        handle_common_layout_properties(
            icon_allow_overlap, icon_anchor, icon_ignore_placement, icon_keep_upright, icon_offset,
            icon_optional, icon_padding, icon_pitch_alignment, icon_rotate, icon_rotation_alignment,
            icon_translate, icon_translate_anchor, symbol_placement, symbol_sort_key,
            "icon_anchor_alignment", "icon_padding_left", "icon_padding_top", "icon_padding_right",
            "icon_padding_bottom", "icon_translate", "icon_position_offset", "icon_rotation",
            "icon_priority", anchor_angle_attribute_name, anchor, padding, transform, expr,
            properties);

        auto* image = transform->mutable_child()->mutable_image();
        image->set_color_blend_mode(hrz_proto::BlendMode::BLEND_MULTIPLY);
        image->set_color_blend_strength(1.0);
        image->set_url(sprite_image_url.data(), sprite_image_url.size());

        if (sprite_index.IsObject())
        {
            for (auto it = sprite_index.MemberBegin(); it != sprite_index.MemberEnd(); ++it)
            {
                parse_mapbox_sprite(it->name.GetString(), it->value, image);
            }
        }

        properties.add(
            "icon_color",
            Property::ColorWithOpacity(
                Property::Generic(Value::from((uint64_t)0xffffff)), icon_opacity),
            image->mutable_color());

        properties.add("icon_scale", icon_size, image->mutable_scale());

        properties.add("icon_sprite_name", icon_image, image->mutable_sprite_name());

        if (has_text)
        {
            hrz_proto::BoxFitAxes fit_axes = HrzProtocol::BoxFitAxes::BOX_FIT_AXES_NONE;
            if (icon_text_fit.str == "width")
            {
                fit_axes = hrz_proto::BoxFitAxes::BOX_FIT_AXES_WIDTH;
            }
            else if (icon_text_fit.str == "height")
            {
                fit_axes = hrz_proto::BoxFitAxes::BOX_FIT_AXES_HEIGHT;
            }
            else if (icon_text_fit.str == "both")
            {
                fit_axes = hrz_proto::BoxFitAxes::BOX_FIT_AXES_BOTH;
            }

            image->set_fit_axes(fit_axes);

            if (fit_axes != hrz_proto::BoxFitAxes::BOX_FIT_AXES_NONE)
            {
                image->set_fit_mode(hrz_proto::BoxFit::BOX_FIT_FILL);

                hrz_proto::SymbolElement icon_tree(stack->children(0));

                stack->mutable_children(0)->set_type(
                    hrz_proto::SymbolElementType::STACK_EXPAND_SYMBOL_ELEMENT);
                auto* stack_expand = stack->mutable_children(0)->mutable_stack_expand();
                stack_expand->mutable_child()->CopyFrom(icon_tree);

                anchor = stack_expand->mutable_child()->mutable_anchor();
                padding = anchor->mutable_child()->mutable_padding();
                transform = padding->mutable_child()->mutable_transform();
                image = transform->mutable_child()->mutable_image();
            }
        }

        // Remove useless elements
        if (transform->components_size() == 0)
        {
            hrz_proto::SymbolElement transform_child(transform->child());
            transform = nullptr;

            padding->mutable_child()->CopyFrom(transform_child);
            image = padding->mutable_child()->mutable_image();
        }
    }

    if (has_text)
    {
        auto* child = stack->add_children();
        child->set_type(hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);

        auto* anchor = child->mutable_anchor();
        anchor->mutable_child()->set_type(hrz_proto::SymbolElementType::PADDING_SYMBOL_ELEMENT);

        auto* padding = anchor->mutable_child()->mutable_padding();
        padding->mutable_child()->set_type(hrz_proto::SymbolElementType::TRANSFORM_SYMBOL_ELEMENT);

        auto* transform = padding->mutable_child()->mutable_transform();
        transform->mutable_child()->set_type(hrz_proto::SymbolElementType::TEXT_SYMBOL_ELEMENT);

        auto* text = transform->mutable_child()->mutable_text();

        handle_common_layout_properties(
            text_allow_overlap, text_anchor, text_ignore_placement, text_keep_upright, text_offset,
            text_optional, text_padding, text_pitch_alignment, text_rotate, text_rotation_alignment,
            text_translate, text_translate_anchor, symbol_placement, symbol_sort_key,
            "text_anchor_alignment", "text_padding_left", "text_padding_top", "text_padding_right",
            "text_padding_bottom", "text_translate", "text_position_offset", "text_rotation",
            "priority", anchor_angle_attribute_name, anchor, padding, transform, expr, properties);

        if (text_font.node && expr.nodes[text_font.node].type == Node::Type::Array)
        {
            // We don't support font stacks, so we'll just use the first one that has a defined
            // URL.
            for (const auto child : expr.nodes[text_font.node].children)
            {
                if (expr.nodes[child].type == Node::Type::Literal
                    && expr.nodes[child].literal.type == Value::Type::String)
                {
                    auto it = font_family_urls.find(expr.nodes[child].literal.str);
                    if (it != font_family_urls.end())
                    {
                        text->set_font_url(it->second);
                        break;
                    }
                }
            }
        }

        properties.add(
            "alignment", Property::TextAlignment(text_justify, text_anchor),
            text->mutable_alignment());
        properties.add("line_spacing", text_line_height, text->mutable_line_spacing());
        properties.add(
            "text_color", Property::ColorWithOpacity(text_color, text_opacity),
            text->mutable_text_color());
        properties.add("text_size", text_size, text->mutable_font_size());
        properties.add("outline_width", text_halo_width, text->mutable_outline_width());
        text->set_outline_width_unit(hrz_proto::TextOutlineWidthUnit::OUTLINE_WIDTH_IN_FONT_UNIT);
        properties.add("text_outline_color", text_halo_color, text->mutable_outline_color());

        properties.add("text", text_field, text->mutable_text());

        if (has_icon && icon_text_fit.str != "none")
        {
            hrz_proto::SymbolElement text_tree(stack->children(1));

            stack->mutable_children(1)->set_type(
                hrz_proto::SymbolElementType::PADDING_SYMBOL_ELEMENT);
            auto* text_fit_padding = stack->mutable_children(1)->mutable_padding();
            text_fit_padding->mutable_child()->CopyFrom(text_tree);

            anchor = text_fit_padding->mutable_child()->mutable_anchor();
            padding = anchor->mutable_child()->mutable_padding();
            transform = padding->mutable_child()->mutable_transform();
            text = transform->mutable_child()->mutable_text();

            Property::Generic padding_left(Value::from(0.0));
            Property::Generic padding_top(Value::from(0.0));
            Property::Generic padding_right(Value::from(0.0));
            Property::Generic padding_bottom(Value::from(0.0));

            if (icon_text_fit_padding.node != NO_NODE)
            {
                unwrap_array_properties(
                    icon_text_fit_padding,
                    {&padding_top, &padding_right, &padding_bottom, &padding_left}, expr);
            }

            properties.add(
                "icon_text_fit_padding_left", padding_left,
                text_fit_padding->mutable_left_padding());
            properties.add(
                "icon_text_fit_padding_top", padding_top, text_fit_padding->mutable_top_padding());
            properties.add(
                "icon_text_fit_padding_right", padding_right,
                text_fit_padding->mutable_right_padding());
            properties.add(
                "icon_text_fit_padding_bottom", padding_bottom,
                text_fit_padding->mutable_bottom_padding());
        }

        // Remove useless elements
        if (transform->components_size() == 0)
        {
            hrz_proto::SymbolElement transform_child(transform->child());
            transform = nullptr;

            padding->mutable_child()->CopyFrom(transform_child);
            text = padding->mutable_child()->mutable_text();
        }
    }

    if (stack->children_size() == 1)
    {
        hrz_proto::SymbolElement stack_child(stack->children(0));
        stack = nullptr;

        symbol->mutable_root_element()->CopyFrom(stack_child);
    }

    return true;
}

bool parse_vector_layer(
    ParseContext* ctx,
    const char* layer_id,
    const rapidjson::Value& json_layer,
    const rapidjson::Value& sprite_index,
    MapboxVectorReprType repr_type)
{
    VectorSource* vector_source = nullptr;

    auto source_name = json::get_str_or(json_layer, "source", "");
    auto source_layer_name = json::get_str_or(json_layer, "source-layer", "");

    auto source_it = ctx->source_names_to_prototypes.find(source_name);
    if (source_it == ctx->source_names_to_prototypes.end())
    {
        HRZ_LOG_ERROR(
            "Missing source \"{}\" for fill-extrusion layer \"{}\".", source_name, layer_id);
        return false;
    }

    const SourcePrototype& source_prototype = source_it->second;
    if (source_prototype.index() != VectorDataSourcePrototypeIndex)
    {
        HRZ_LOG_ERROR(
            "Vector layer \"{}\" requires its source \"{}\" to be of type 'vector'.", layer_id,
            source_name);
        return false;
    }

    auto source_hash = hash_vector_source(source_name, source_layer_name);
    auto it = ctx->vector_source_hashes_to_data.find(source_hash);
    if (it == ctx->vector_source_hashes_to_data.end())
    {
        // Create a new vector data layer based on the source prototype.

        const auto& prototype = std::get<hrz_proto::VectorDataLayer>(source_prototype);

        auto pair = ctx->vector_source_hashes_to_data.insert({source_hash, {}});
        vector_source = &pair.first->second;

        vector_source->name = fmt::format("{} - {}", source_name, source_layer_name);
        vector_source->model.CopyFrom(prototype);
        vector_source->model.set_id(ctx->next_vector_data_layer_id++);

        auto* source = vector_source->model.mutable_sources(0);
        if (source->provider_type()
            == hrz_proto::VectorDataProviderType::TILEJSON_VECTOR_DATA_PROVIDER)
        {
            source->mutable_tilejson_data_provider()->set_layer_name(source_layer_name);
        }
        else if (
            source->provider_type()
            == hrz_proto::VectorDataProviderType::PMTILES_VECTOR_DATA_PROVIDER)
        {
            source->mutable_pmtiles_data_provider()->set_layer_name(source_layer_name);
        }
        else if (
            source->provider_type()
            == hrz_proto::VectorDataProviderType::TILED_VECTOR_DATA_PROVIDER)
        {
            source->mutable_tiled_data_provider()->set_layer_name(source_layer_name);
        }
    }
    else
    {
        // The vector layer can reuse an already existing vector data layer. New attributes, unused
        // by previous vector tile layers, may be added to the vector data layer.

        vector_source = &it->second;
    }

    hrz_proto::LayerVisibilityConstraintList visibility_constraints;
    bool visible = true;
    CHECK_ERR(parse_common_layer_properties(ctx, json_layer, &visibility_constraints, &visible));

    VectorLayer* vector_layer = nullptr;
    for (auto& layer : vector_source->users)
    {
        if (layer.model.visible() != visible)
        {
            continue;
        }

        if (layer.model.visibility_constraints().constraints_size()
            != visibility_constraints.constraints_size())
        {
            continue;
        }

        bool matching_constraints = true;
        for (size_t i = 0; i < (size_t)visibility_constraints.constraints_size(); ++i)
        {
            const auto& constraint = visibility_constraints.constraints(i);
            const auto& ref = layer.model.visibility_constraints().constraints(i);

            if (constraint.type() != ref.type())
            {
                matching_constraints = false;
                break;
            }

            if (constraint.type() == hrz_proto::LayerVisibilityConstraintType::ALTITUDE)
            {
                if (constraint.altitude().relative_position() != ref.altitude().relative_position())
                {
                    matching_constraints = false;
                    break;
                }

                if (constraint.altitude().altitude() != ref.altitude().altitude())
                {
                    matching_constraints = false;
                    break;
                }
            }
            else if (constraint.type() == hrz_proto::LayerVisibilityConstraintType::BOUNDS)
            {
                if (constraint.bounds().relative_position() != ref.bounds().relative_position())
                {
                    matching_constraints = false;
                    break;
                }

                if (constraint.bounds().bounds().west() != ref.bounds().bounds().west()
                    || constraint.bounds().bounds().east() != ref.bounds().bounds().east()
                    || constraint.bounds().bounds().south() != ref.bounds().bounds().south()
                    || constraint.bounds().bounds().north() != ref.bounds().bounds().north())
                {
                    matching_constraints = false;
                    break;
                }
            }
            else
            {
                assert(false && "Unhandled visibility constraint type");
                matching_constraints = false;
                break;
            }
        }

        if (!matching_constraints)
        {
            continue;
        }

        vector_layer = &layer;
        break;
    }

    if (!vector_layer)
    {
        if (vector_source->users.size() == 1)
        {
            // There is already another layer that uses the same source: add an index to its
            // name so that we don't have duplicate names.
            vector_source->users.front().name += " 1";
        }

        vector_source->users.push_back({});
        vector_layer = &vector_source->users.back();

        if (std::string_view(source_layer_name).empty())
        {
            vector_layer->name = fmt::format("{}", source_name);
        }
        else
        {
            vector_layer->name = fmt::format("{} - {}", source_name, source_layer_name);
        }

        if (vector_source->users.size() > 1)
        {
            vector_layer->name += fmt::format(" {}", vector_source->users.size());
        }

        vector_layer->model.set_visible(visible);
        vector_layer->model.mutable_visibility_constraints()->CopyFrom(visibility_constraints);
        vector_layer->model.mutable_source()->set_vector_data_layer_id(vector_source->model.id());
        vector_layer->model.set_static_tiles(true);
        // Clamping is set later based on the presence of certain types of representations.
        vector_layer->model.mutable_clamping()->set_method(hrz_proto::VectorClampMode::NO_CLAMPING);
        vector_layer->model.mutable_resolution()->set_max_screen_space_error(4);
        vector_layer->model.set_clip_id(-1);
        vector_layer->model.mutable_lighting()->set_enable_lighting(true);
        vector_layer->model.mutable_lighting()->set_cast_shadows(true);
        vector_layer->model.mutable_lighting()->set_receive_shadows(true);
        vector_layer->model.mutable_scene_views()->set_bits(3);
    }

    // Vector data and vector tiles layer creation is deferred, because they can be reused for
    // different Mapbox sources or layers.
    {
        ExpressionContext expr(layer_id, &vector_source->attributes);

        NodeIndex filter_node_index = NO_NODE;
        const auto& filter = json::get_member_or_null(json_layer, "filter");

        if (!filter.IsNull())
        {
            filter_node_index = parse_expression(filter, MapboxPropertyType::Bool, expr);
            if (filter_node_index == NO_NODE)
            {
                HRZ_LOG_ERROR("Failed to parse \"filter\" property of \"{}\" layer.", layer_id);
            }
        }

        const auto& paint = json::get_member_or_null(json_layer, "paint");
        const auto& layout = json::get_member_or_null(json_layer, "layout");

        std::vector<Property> properties;
        PropertyAdder property_adder(&properties, layer_id);

        auto* style = vector_layer->model.mutable_style();

        hrz::InlinedVector<std::string_view, 2> new_representation_names;
        uint32_t first_new_representation_index = style->representations_size();

        auto add_repr = [&](std::string_view name)
        {
            auto id = (uint32_t)style->representations_size();

            auto* repr = style->add_representations();
            repr->set_id(id);
            repr->set_name(name.data(), name.size());
            repr->mutable_scene_views()->set_bits(3);

            new_representation_names.push_back(repr->name());

            return repr;
        };

        switch (repr_type)
        {
            case MapboxVectorReprType::FillExtrusion:
            {
                auto* repr = add_repr(layer_id);
                repr->set_type(hrz_proto::VectorReprType::EXTRUDED_GEOMETRY_VECTOR_REPR);

                create_fill_extrusion_repr(
                    paint, repr->mutable_extruded_geometry(), expr, property_adder);
                break;
            }
            case MapboxVectorReprType::Fill:
            {
                auto* repr = add_repr(layer_id);
                repr->set_type(hrz_proto::VectorReprType::FLAT_OVERLAY_VECTOR_REPR);

                std::optional<hrz_proto::FlatOverlayVectorRepr> outline_flat_overlay;

                create_fill_repr(
                    paint, layout, sprite_index, ctx->sprite_image_url,
                    repr->mutable_flat_overlay_geometry(), &outline_flat_overlay,
                    &ctx->next_flat_overlay_z_index, expr, property_adder);

                if (outline_flat_overlay.has_value())
                {
                    auto* outline_repr = add_repr(fmt::format("{}_hrz_outline", layer_id));
                    outline_repr->set_type(hrz_proto::FLAT_OVERLAY_VECTOR_REPR);
                    outline_repr->mutable_flat_overlay_geometry()->CopyFrom(
                        outline_flat_overlay.value());
                }

                break;
            }
            case MapboxVectorReprType::Line:
            {
                auto* repr = add_repr(layer_id);
                repr->set_type(hrz_proto::VectorReprType::FLAT_OVERLAY_VECTOR_REPR);

                create_line_repr(
                    paint, layout, repr->mutable_flat_overlay_geometry(),
                    &ctx->next_flat_overlay_z_index, expr, property_adder);
                break;
            }
            case MapboxVectorReprType::Circle:
            {
                auto* repr = add_repr(layer_id);
                repr->set_type(hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);

                create_circle_repr(
                    paint, layout, repr->mutable_symbol(), &ctx->next_symbol_z_index, expr,
                    property_adder);
                break;
            }
            case MapboxVectorReprType::Heatmap:
            {
                auto* repr = add_repr(layer_id);
                repr->set_type(hrz_proto::VectorReprType::HEATMAP_VECTOR_REPR);

                create_heatmap_repr(
                    paint, layout, repr->mutable_heatmap(), &ctx->next_flat_overlay_z_index, expr,
                    property_adder);
                break;
            }
            case MapboxVectorReprType::Symbol:
            {
                auto* repr = add_repr(layer_id);
                repr->set_type(hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR);

                create_symbol_repr(
                    paint, layout, sprite_index, ctx->sprite_image_url, ctx->font_names_to_urls,
                    repr->mutable_symbol(), style->mutable_anchor_angle_attribute_name(),
                    &ctx->next_symbol_z_index, expr, property_adder);
                break;
            }
            default:
            {
                assert(false && "Unhandled case");
                return false;
            }
        }

        finalize_expression(filter_node_index, expr);
        for (auto& property : properties)
        {
            property.visit_nodes([&](NodeIndex root) { finalize_expression(root, expr); });
        }

        auto repr_clamp_mode = [](MapboxVectorReprType type)
        {
            switch (type)
            {
                case MapboxVectorReprType::FillExtrusion:
                    return hrz_proto::VectorClampMode::PER_VERTEX;

                case MapboxVectorReprType::Symbol:
                case MapboxVectorReprType::Circle: return hrz_proto::VectorClampMode::ANCHOR;

                case MapboxVectorReprType::Line:
                case MapboxVectorReprType::Fill:
                case MapboxVectorReprType::Heatmap: return hrz_proto::VectorClampMode::NO_CLAMPING;

                default:
                    assert(false && "Unhandled");
                    return hrz_proto::VectorClampMode::NO_CLAMPING;
            }
        }(repr_type);

        // We don't want to assign the less-precise "Anchor" clamp mode if another representation
        // wants to use the "Per vertex" mode.
        if (vector_layer->model.clamping().method() == hrz_proto::VectorClampMode::NO_CLAMPING
            || repr_clamp_mode == hrz_proto::VectorClampMode::PER_VERTEX)
        {
            vector_layer->model.mutable_clamping()->set_method(repr_clamp_mode);
        }

        if (first_new_representation_index < (size_t)style->representations_size())
        {
            assert(!new_representation_names.empty());
            generate_representations_script(
                expr.nodes, properties, new_representation_names,
                style->representations(first_new_representation_index).id(), filter_node_index,
                *style->mutable_styling_script());
        }
    }

    return true;
}

bool parse_background_layer(
    ParseContext* ctx,
    const char* layer_id,
    const rapidjson::Value& json_layer)
{
    bool visible = true;

    const auto& layout_node = hrz::json::get_member_or_null(json_layer, "layout");
    auto visibility_opt = hrz::json::get_str(layout_node, "visibility");
    if (visibility_opt.has_value())
    {
        if (visibility_opt.value() == std::string_view("none"))
        {
            visible = false;
        }
    }

    hrz_proto::Color color;
    color.set_r(0.0f);
    color.set_g(0.0f);
    color.set_b(0.0f);
    color.set_a(1.0f);

    const auto& paint_node = hrz::json::get_member_or_null(json_layer, "paint");
    auto background_color_opt = hrz::json::get_str(paint_node, "background-color");
    if (background_color_opt.has_value())
    {
        uint32_t background_color{};
        if (parse_mapbox_color_string(background_color_opt.value(), &background_color))
        {
            color = convert_uint_to_proto_color(background_color);
        }
    }

    if (visible)
    {
        for (auto& scene_view_settings : *ctx->scene_dump->mutable_scene_view_settings())
        {
            scene_view_settings.mutable_settings()
                ->mutable_terrain()
                ->mutable_terrain_color()
                ->CopyFrom(color);
        }
    }

    warn_unsupported_member(paint_node, "background-emissive-strength");
    warn_unsupported_member(paint_node, "background-opacity");
    warn_unsupported_member(paint_node, "background-pattern");
    warn_unsupported_member(json_layer, "layout");

    return true;
}

bool parse_layer(
    ParseContext* ctx,
    const rapidjson::Value& layer,
    const rapidjson::Value& sprite_index)
{
    // Layers
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/layers/

    CHECK_ERR_M("Layer is not an object.", layer.IsObject());
    auto id = json::get_str(layer, "id");
    CHECK_ERR_M("Layer has no id", id.has_value());
    auto type_opt = json::get_str(layer, "type");
    CHECK_ERR_M("Layer has no type", type_opt.has_value());

    std::string_view type(type_opt.value());
    if (type == std::string_view("background"))
    {
        CHECK_ERR(parse_background_layer(ctx, id.value(), layer));
    }
    else if (type == std::string_view("raster"))
    {
        CHECK_ERR(parse_raster_layer(ctx, id.value(), layer));
    }
    else if (type == std::string_view("fill"))
    {
        CHECK_ERR(
            parse_vector_layer(ctx, id.value(), layer, sprite_index, MapboxVectorReprType::Fill));
    }
    else if (type == std::string_view("line"))
    {
        CHECK_ERR(
            parse_vector_layer(ctx, id.value(), layer, sprite_index, MapboxVectorReprType::Line));
    }
    else if (type == std::string_view("symbol"))
    {
        CHECK_ERR(
            parse_vector_layer(ctx, id.value(), layer, sprite_index, MapboxVectorReprType::Symbol));
    }
    else if (type == std::string_view("circle"))
    {
        CHECK_ERR(
            parse_vector_layer(ctx, id.value(), layer, sprite_index, MapboxVectorReprType::Circle));
    }
    else if (type == std::string_view("heatmap"))
    {
        CHECK_ERR(parse_vector_layer(
            ctx, id.value(), layer, sprite_index, MapboxVectorReprType::Heatmap));
    }
    else if (type == std::string_view("fill-extrusion"))
    {
        CHECK_ERR(parse_vector_layer(
            ctx, id.value(), layer, sprite_index, MapboxVectorReprType::FillExtrusion));
    }
    else if (type == std::string_view("model"))
    {
        CHECK_ERR(
            parse_vector_layer(ctx, id.value(), layer, sprite_index, MapboxVectorReprType::Model));
    }
    else
    {
        HRZ_LOG_WARNING("Unsupported layer type {}", type.data());
    }

    return true;
}
} // namespace

TranslationResult translate_scene(
    std::string_view json,
    std::string_view sprite_index_json,
    const TranslationSettings& settings,
    hrz_proto::SceneDump* scene_dump)
{
    TranslationResult result;
    result.success = false;

    rapidjson::Document root;
    root.Parse(json.data(), json.size());

    if (root.HasParseError())
    {
        HRZ_LOG_ERROR(
            "Failed to parse Mapbox style JSON ({}): {}", root.GetErrorOffset(),
            rapidjson::GetParseError_En(root.GetParseError()));
        return result;
    }

    if (!root.IsObject())
    {
        HRZ_LOG_ERROR("Incorrect Mapbox style (JSON root is not an object).");
        return result;
    }

    rapidjson::Document sprite_index;

    if (!sprite_index_json.empty())
    {
        sprite_index.Parse(sprite_index_json.data(), sprite_index_json.size());

        if (sprite_index.HasParseError())
        {
            HRZ_LOG_ERROR(
                "Failed to parse Mapbox sprite index JSON ({}): {}", sprite_index.GetErrorOffset(),
                rapidjson::GetParseError_En(sprite_index.GetParseError()));
            return result;
        }

        if (!sprite_index.IsObject() && !sprite_index.IsNull())
        {
            HRZ_LOG_ERROR("Incorrect Mapbox style sprite index (JSON root is not an object).");
            return result;
        }
    }

    ParseContext ctx = {};
    ctx.scene_dump = scene_dump;
    ctx.settings = &settings;
    ctx.next_raster_slot = settings.first_raster_slot;
    ctx.next_vector_data_layer_id = settings.first_vector_data_layer_id;
    ctx.next_flat_overlay_z_index = settings.first_flat_overlay_z_index;
    ctx.next_symbol_z_index = settings.first_symbol_z_index;

    ctx.scene_dump->Clear();

    for (int i = 0; i < hrz_proto::SceneViewIndex_ARRAYSIZE; ++i)
    {
        auto* view_settings = ctx.scene_dump->add_scene_view_settings();
        auto index = (hrz_proto::SceneViewIndex)((int)hrz_proto::SceneViewIndex_MIN + i);

        view_settings->set_index(index);
        view_settings->mutable_settings()->CopyFrom(default_scene_view_settings());

        view_settings->mutable_settings()->mutable_ambient()->mutable_sky()->set_attenuation(0.8);
        view_settings->mutable_settings()->mutable_ambient()->mutable_sky()->set_mode(
            hrz_proto::SkyMode::SKY_STATIC);
        view_settings->mutable_settings()
            ->mutable_ambient()
            ->mutable_lighting()
            ->set_enable_lighting(true);
        view_settings->mutable_settings()->mutable_ambient()->mutable_lighting()->set_cast_shadows(
            false);
        view_settings->mutable_settings()
            ->mutable_ambient()
            ->mutable_lighting()
            ->set_receive_shadows(false);

        // Set the date to noon of the vernal equinox to hopefully avoid obscuring relevant
        // parts of the map
        view_settings->mutable_settings()
            ->mutable_ambient()
            ->mutable_sun()
            ->mutable_direction()
            ->set_mode(hrz_proto::SunDirectionMode::SUN_DIRECTION_RELATIVE_TO_TANGENTIAL_FRAME);
        view_settings->mutable_settings()
            ->mutable_ambient()
            ->mutable_sun()
            ->mutable_direction()
            ->set_azimuth(150.0 * lm::PI / 180.0);
        view_settings->mutable_settings()
            ->mutable_ambient()
            ->mutable_sun()
            ->mutable_direction()
            ->set_altitude(60.0 * lm::PI / 180.0);

        // Use the default MapLibre background color as default terrain color
        auto* terrain_color =
            view_settings->mutable_settings()->mutable_terrain()->mutable_terrain_color();
        terrain_color->set_r(1.0);
        terrain_color->set_g(1.0);
        terrain_color->set_b(1.0);
        terrain_color->set_a(1.0);

        // Use the default Mapbox colors for the atmosphere: "highColor" and "spaceColor"
        auto* atmosphere_color = view_settings->mutable_settings()
                                     ->mutable_ambient()
                                     ->mutable_sky()
                                     ->mutable_static_atmosphere_color();
        atmosphere_color->set_r(0.141);
        atmosphere_color->set_g(0.360);
        atmosphere_color->set_b(0.8745);
        atmosphere_color->set_a(1.00);
        auto* space_color = view_settings->mutable_settings()
                                ->mutable_ambient()
                                ->mutable_sky()
                                ->mutable_static_space_color();
        space_color->set_r(0.043);
        space_color->set_g(0.043);
        space_color->set_b(0.098);
        space_color->set_a(1.0);
        view_settings->mutable_settings()
            ->mutable_ambient()
            ->mutable_sky()
            ->set_static_color_transition_start_distance(15000);
        view_settings->mutable_settings()
            ->mutable_ambient()
            ->mutable_sky()
            ->set_static_color_transition_end_distance(60000);
        view_settings->mutable_settings()
            ->mutable_ambient()
            ->mutable_sky()
            ->set_static_color_transition_distance_unit(
                hrz_proto::StaticSkyColorTransitionUnit::STATIC_SKY_COLOR_TRANSITION_UNIT_METERS);
    }

    // Root properties
    // https://docs.mapbox.com/mapbox-gl-js/style-spec/root/

    auto version = json::get_int(root, "version");
    if (!version.has_value())
    {
        HRZ_LOG_ERROR("Missing member \"version\" in root");
        return result;
    }

    if (version.value() < MapboxMinSupportedVersion || version.value() > MapboxMaxSupportedVersion)
    {
        HRZ_LOG_ERROR(
            "Unsupported Mapbox style version {} : Versions {} to {} are supported",
            version.value(), MapboxMinSupportedVersion, MapboxMaxSupportedVersion);
        return result;
    }

    scene_dump->set_name(json::get_str_or(root, "name", ""));

    {
        hrz_proto::CameraDump* camera = scene_dump->add_cameras();

        const auto& center = json::get_member_or_null(root, "center");
        if (center.IsArray() && center.Size() == 2)
        {
            const auto longitude = json::as_double(json::get_nth_or_null(center, 0)).value_or(0);
            const auto latitude = json::as_double(json::get_nth_or_null(center, 1)).value_or(0);

            camera->mutable_viewpoint()->mutable_target()->set_longitude(longitude);
            camera->mutable_viewpoint()->mutable_target()->set_latitude(latitude);
        }

        const auto tilt = lm::radians(json::get_double_or(root, "pitch", 0));
        const auto bearing = lm::radians(json::get_double_or(root, "bearing", 0));

        camera->mutable_viewpoint()->set_tilt(tilt);
        camera->mutable_viewpoint()->set_bearing(bearing);

        const auto zoom_level = json::get_double_or(root, "zoom", 0);
        const double distance = approximate_camera_distance(
            lm::radians(camera->viewpoint().target().latitude()), zoom_level,
            settings.viewport_size, settings.camera_fovy);
        camera->mutable_viewpoint()->set_distance(distance);
    }

    const auto sprite_opt = hrz::json::get_str(root, "sprite");
    if (sprite_opt.has_value())
    {
        ctx.sprite_image_url = fmt::format("{}.png", sprite_opt.value());
    }

    static constexpr const char* hrz_fonts_property = "hrz:fonts";

    const auto& metadata = hrz::json::get_member_or_null(root, "metadata");
    if (metadata != json::NullValue)
    {
        const auto& font_array = hrz::json::get_member_or_null(metadata, hrz_fonts_property);
        if (font_array != hrz::json::NullValue)
        {
            if (!font_array.IsObject())
            {
                HRZ_LOG_ERROR("\"{}\" metadata property should be an object.", hrz_fonts_property);
            }
            else
            {
                for (auto it = font_array.MemberBegin(); it != font_array.MemberEnd(); ++it)
                {
                    if (!it->value.IsString())
                    {
                        HRZ_LOG_ERROR(
                            "\"{}\" metadata property members should be strings.",
                            hrz_fonts_property);
                        continue;
                    }
                    ctx.font_names_to_urls.insert({it->name.GetString(), it->value.GetString()});
                }
            }
        }
    }

    if (hrz::json::get_str(root, "glyphs").has_value() && ctx.font_names_to_urls.empty())
    {
        HRZ_LOG_WARNING(
            "Horizon does not support the \"glyphs\" property for handling fonts. A new \"{}\" "
            "property should be added to the root \"metadata\" instead, consisting of an object "
            "mapping font families (as specified in \"text-font\" property or the "
            "\"{{fontstack}}\" "
            "template parameter of \"glyphs\") to an URL pointing to a TTF font to use instead.",
            hrz_fonts_property);
    }

    const auto& sources = json::get_member_or_null(root, "sources");
    if (sources != json::NullValue && sources.IsObject())
    {
        for (const auto& source : sources.GetObject())
        {
            auto source_name = source.name.GetString();
            if (!parse_source(&ctx, source_name, source.value))
            {
                HRZ_LOG_WARNING("Failed to parse source \"{}\".", source_name);
            }
        }
    }

    const auto& layers = json::get_member_or_null(root, "layers");
    if (layers != json::NullValue && layers.IsArray())
    {
        for (const auto& layer : layers.GetArray())
        {
            if (!parse_layer(&ctx, layer, sprite_index))
            {
                HRZ_LOG_WARNING("Failed to parse layer \"{}\".", json::get_str_or(layer, "id", ""));
            }
        }
    }

    // Register all the vector data and vector tiles layers now that we are sure they are complete.
    for (const auto& it : ctx.vector_source_hashes_to_data)
    {
        const auto& source_data = it.second;

        auto* layer = ctx.scene_dump->add_layers();
        layer->set_name(source_data.name);
        layer->mutable_vector_data()->CopyFrom(source_data.model);

        auto* source = layer->mutable_vector_data()->mutable_sources(0);
        for (const auto& attr_it : source_data.attributes)
        {
            auto* attr = source->add_attributes();
            attr->set_id(attr_it.second.id);
            attr->set_source_name(attr_it.first);
        }

        for (const auto& user : source_data.users)
        {
            auto* layer = ctx.scene_dump->add_layers();
            layer->set_name(user.name);

            auto* vector_tiles = layer->mutable_vector_tiles();
            vector_tiles->CopyFrom(user.model);

            // Create attribute references.
            for (const auto& it : source_data.attributes)
            {
                auto* attr_ref = vector_tiles->mutable_style()->add_attributes();
                attr_ref->set_vector_data_attr_id(it.second.id);
                attr_ref->set_styling_name(it.first);
            }
        }
    }

    warn_unsupported_member(root, "fog");
    warn_unsupported_member(root, "light");
    warn_unsupported_member(root, "projection");
    warn_unsupported_member(root, "terrain");
    warn_unsupported_member(root, "transition");

    scene_dump->set_version(hrz::scene_dump::SceneModelVersion);

    result.success = true;
    result.raster_slots_used = ctx.next_raster_slot - settings.first_raster_slot;
    result.vector_data_layer_ids_used =
        ctx.next_vector_data_layer_id - settings.first_vector_data_layer_id;
    result.flat_overlay_z_indices_used =
        ctx.next_flat_overlay_z_index - settings.first_flat_overlay_z_index;
    result.symbol_z_indices_used = ctx.next_symbol_z_index - settings.first_symbol_z_index;

    return result;
}
} // namespace hrz_mapbox
