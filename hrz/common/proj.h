// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/geo.h"

#include <lin_maths.h>
#include <proj_lite.h>

namespace hrz_proj
{

static const char* const wmerc_proj_str =
    "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
    "+nadgrids=@null +wktext +no_defs";
static const char* const ecef_proj_str = "+proj=geocent +datum=WGS84 +no_defs";
static const char* const lonlat_deg_proj_str = "+proj=longlat +datum=WGS84 +no_defs";
static const char* const lonlat_rad_proj_str = "+proj=longlat +datum=WGS84 +units=rad +no_defs";

extern pl_Transform wmerc_to_ecef;
extern pl_Transform wmerc_to_lonlat_deg;
extern pl_Transform wmerc_to_lonlat_rad;
extern pl_Transform ecef_to_wmerc;
extern pl_Transform lonlat_deg_to_wmerc;
extern pl_Transform lonlat_rad_to_wmerc;
extern pl_Transform lonlat_deg_to_ecef;
extern pl_Transform lonlat_rad_to_ecef;

extern pl_Crs wmerc;
extern pl_Crs ecef;
extern pl_Crs lonlat_deg;
extern pl_Crs lonlat_rad;

static const lm::dbbox2 web_mercator_bounds{
    {-hrz::HALF_MERCATOR_RANGE, -hrz::HALF_MERCATOR_RANGE},
    {hrz::HALF_MERCATOR_RANGE, hrz::HALF_MERCATOR_RANGE},
};
static const lm::dbbox2 wgs84_bounds{
    {-180, -90},
    {180, 90},
};

void init_global_common_transforms();

} // namespace hrz_proj
