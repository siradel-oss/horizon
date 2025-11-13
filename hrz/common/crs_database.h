#pragma once

#include "hrz/protocol/all.h"

#include <proj_lite.h>

#include <string>
#include <string_view>

namespace hrz
{
bool convert_crs(std::string_view descriptor, hrz_proto::SrsDescriptorType, pl_Crs* crs);
bool convert_crs(std::string_view descriptor, hrz_proto::SpatialReferenceSystem*);
bool convert_crs(std::string_view descriptor, pl_Crs* crs);
bool convert_crs(const hrz_proto::SpatialReferenceSystem&, pl_Crs* crs);
bool check_crs(const hrz_proto::SpatialReferenceSystem&);
std::string srid_descriptor_to_proj4(std::string_view descriptor);
} // namespace hrz
