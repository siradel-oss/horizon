#pragma once

#include "hrz/protocol/identification/picking_result.pb.h"

#include <cstdint>
#include <vector>

namespace hrz::planet
{

using RasterDataFetchTicket = uint32_t;
using RasterDataFetchMergeGroupTicket = uint32_t;

struct RasterDataFetchResult
{
    std::vector<hrz_proto::PickLayerResult> results;
};

} // namespace hrz::planet
