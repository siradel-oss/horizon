#pragma once

#include "hrz/common/blob_image.h"
#include "hrz/common/palette.h"
#include "hrz/protocol/raster/nodata.pb.h"

#include <lin_maths.h>

namespace hrz_jobs
{

struct PalettizeImageParams
{
    hrz::BlobImage image;
    hrz_proto::RasterNodata nodata;
    hrz::Palette palette;
    lm::ubvec4 nodata_color_srgb;
};

} // namespace hrz_jobs
