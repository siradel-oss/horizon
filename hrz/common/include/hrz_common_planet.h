#pragma once

#include "hrz_common_blob_allocator.h"
#include "hrz_common_blob_array.h"
#include "hrz_common_blob_array_view.h"
#include "hrz_common_blob_image.h"
#include "hrz_common_palette.h"
#include "hrz_common_proto_maths.h"
#include "hrz_common_tile_coords.h"

#include <hrz_fnd_inlined_vector.h>
#include <hrz_fnd_variant.h>
#include <hrz_protocol_all.h>

#include <lin_maths.h>

#include <vector>

namespace hrz::planet
{

// This mirrors the hrz_proto::RasterGeometry message, but adds the tiling scheme.
struct TiledRasterGeometry
{
    static TiledRasterGeometry from_geometry_and_tiling_scheme(
        const hrz_proto::RasterGeometry& geometry,
        const hrz_proto::TilingSchemeParams& tiling_scheme)
    {
        TiledRasterGeometry result;
        result.projection = geometry.projection();
        result.projection_bounds = to_lm(geometry.projection_bounds());
        result.bounds = to_lm(geometry.bounds());
        result.tiling_scheme = tiling_scheme;
        return result;
    }

    hrz_proto::SpatialReferenceSystem projection;
    lm::dbbox2 projection_bounds;
    lm::dbbox2 bounds;
    hrz_proto::TilingSchemeParams tiling_scheme;
};

struct FeedbackData
{
    hrz::BlobImage image;
    std::vector<lm::uvec2> clipmap_offsets;
};

enum TileRequestOrigin : uint8_t
{
    FeedbackOrigin = 1 << 0,
    CameraVerticalProjectionOrigin = 1 << 1,
};

struct RequestedTileCoords
{
    TileCoords coords;
    uint32_t uses;
    TileRequestOrigin origin;
};

struct TileList
{
    std::vector<RequestedTileCoords> tile_usage;
};

struct RasterTileReprojParams
{
    hrz::TileCoords tile_coords;
    hrz::planet::TiledRasterGeometry raster_geometry;
    // In Web Mercator (EPSG:3857)
    lm::dbbox2 raster_display_bounds;
    uint32_t quad_size;
};

struct ReprojectedTiles
{
    struct ReprojectedTile
    {
        hrz::TileCoords coords;
        lm::uvec2 grid_size;
        std::vector<float> grid_coords;
        lm::bbox2 uv_clip;
    };

    std::vector<ReprojectedTile> tiles;
};

struct MipmapGenerationParams
{
    hrz::BlobImage image;
    uint32_t tile_size;
    hrz_proto::RasterNodata nodata;
};

struct Mipmaps
{
    struct Tile
    {
        hrz::TileCoords coords;
        hrz::BlobImage image;
    };

    std::vector<Tile> tiles;
};

struct RasterTileCompositionParams
{
    struct ReprojectionMesh
    {
        std::vector<float> grid;
        lm::uvec2 quad_count;
        lm::bbox2 uv_clip;
        hrz::TileCoords tile_coords;
        hrz::TileCoords tile_image_coords;
    };

    struct Blit
    {
        hrz::TileCoords input_coords;
        lm::bbox2 uv_clip;
    };

    struct ImageWithCanvas
    {
        std::variant<ReprojectionMesh, Blit> canvas;
        hrz::BlobImage image;
        hrz_proto::RasterNodata nodata;
        hrz_proto::RasterSampling sampling;
        hrz_proto::RasterBlending blending;
        lm::bbox2 dst_uv_clip;
    };

    std::vector<ImageWithCanvas> images;
    hrz::TileCoords output_coords;
    hrz_proto::ImageFormat output_format;

    bool compute_value_bounds;
    double (*pixel_to_value)(const void* pixel);
};

struct RasterTileCompositionResponse
{
    hrz::BlobImage image;
    std::optional<double> min_value;
    std::optional<double> max_value;
};

using ElevationQueryPointStorage = std::variant<hrz::BlobArrayView<lm::dvec2>, lm::dvec2>;

struct SamplePointsQueryParams
{
    struct Raster
    {
        hrz_proto::ImageFormat image_format;
        hrz::planet::TiledRasterGeometry geometry;
        // In Web Mercator (EPSG:3857)
        lm::dbbox2 display_bounds;
        hrz_proto::RasterNodata nodata;
        hrz_proto::RasterBlending blending;
        hrz_proto::RasterSampling sampling;
    };

    struct TileWithImage
    {
        uint32_t raster_index;
        hrz::TileCoords tile_coords;
        hrz::BlobImage image;
    };

    std::vector<Raster> rasters;
    ElevationQueryPointStorage points;
    std::vector<TileWithImage> tiles;
};

struct SampledPointsQuery
{
    hrz::BlobArray<float> values;
};

struct PalettizeImageParams
{
    hrz::BlobImage image;
    hrz_proto::RasterNodata nodata;
    hrz::Palette palette;
    lm::ubvec4 nodata_color_srgb;
};

struct CesiumTerrainTileData
{
    hrz::blobs::BlobHandle blob;
    std::string format;
};

struct CullPointsQueryParams
{
    struct Raster
    {
        hrz::planet::TiledRasterGeometry geometry;
        // In Web Mercator (EPSG:3857)
        lm::dbbox2 display_bounds;
    };

    hrz::InlinedVector<Raster, 4> rasters;
    ElevationQueryPointStorage points;
};

struct CulledPointsQuery
{
    struct Tile
    {
        uint32_t raster_index;
        hrz::TileCoords coords;
    };

    hrz::InlinedVector<Tile, 16> tiles;
};

struct TilemapResourceParams
{
    hrz::blobs::BlobHandle raw_xml;
};

struct TilemapResourceResponse
{
    std::vector<std::string> url_patterns;
    hrz::planet::TiledRasterGeometry geometry;
    std::string attribution_title;
    std::string attribution_logo;
};

struct WmtsResourceParams
{
    std::string wmts_url;
    hrz::blobs::BlobHandle raw_xml;
    std::string layer_identifier;
    std::string style_identifier;
    std::string image_format;
};

enum class WmtsGetTileMethod
{
    GET_RESTFUL,
    GET_KVP,
};

struct WmtsResourceResponse
{
    WmtsGetTileMethod get_tile_method;
    std::vector<std::string> url_patterns;
    std::vector<std::string> matrix_identifiers;
    hrz::planet::TiledRasterGeometry geometry;
};

struct WmsResourceParams
{
    struct StyledLayer
    {
        std::string layer_name;
        std::string style_name;
    };

    struct Dimension
    {
        std::string name;
        std::string value;
    };

    std::string wms_url;
    hrz::blobs::BlobHandle raw_xml;
    std::vector<StyledLayer> layers;
    std::vector<Dimension> dimensions;
    // Alpha is ignored.
    uint32_t background_color;
    bool force_opaque;
    std::string image_format;
    bool override_min_level;
    uint8_t min_level;
    bool override_max_level;
    uint8_t max_level;
};

struct WmsResourceResponse
{
    std::string url_template;
    hrz::planet::TiledRasterGeometry geometry;

    struct Attribution
    {
        std::string title;
        std::string link;
        std::string logo;
    };

    std::vector<Attribution> attributions;
};
} // namespace hrz::planet
