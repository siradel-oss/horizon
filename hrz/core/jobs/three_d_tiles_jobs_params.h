// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/common/three_d_tiles.h"
#include "hrz/common/vector_data/feature_ids.h"

#include <lin_maths.h>

#include <optional>

namespace hrz_jobs
{

struct EncodedThreeDTilesTileset
{
    hrz::blobs::BlobHandle raw_json;
    lm::dmat4 transform;
    uint32_t root_depth;
};

struct EncodedBatchTable
{
    uint32_t batch_length;
    hrz::blobs::BlobHandle json_data;
    hrz::blobs::BlobHandle bin_data;
    std::vector<hrz::three_d_tiles::AttributeConfig> attributes;
};

struct DecodedBatchTable
{
    hrz::vector_data::FeatureIds batches_to_feature_ids;
    std::vector<std::optional<hrz::vector_data::AttributeValues>> attribute_values;
};

} // namespace hrz_jobs
