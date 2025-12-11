#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/common/planet/tiled_raster_geometry.h"

#include <string>
#include <vector>

namespace hrz_jobs
{

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

} // namespace hrz_jobs
