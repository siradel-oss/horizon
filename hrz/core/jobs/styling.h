// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_array.h"
#include "hrz/common/palette.h"
#include "hrz/common/style/flat_ast.h"
#include "hrz/common/style/styled_features.h"
#include "hrz/common/vector_data/attribute_type_owned.h"
#include "hrz/common/vector_data/feature_id_hash.h"
#include "hrz/common/vector_data/packed_attribute_values.h"
#include "hrz/fnd/flat_hash_map.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hrz_jobs
{

struct FeaturesStylingData
{
    struct Representation
    {
        uint32_t id{};
        std::string name;
    };

    // The AST is passed as a shared pointer to ensure it lives
    // at least as long as the job.
    std::shared_ptr<const hrz::style::FlatAst> ast;
    size_t feature_count;
    // In the same order than palettes were added to the parser.
    std::vector<hrz::Palette> palettes;
    std::vector<Representation> representations;
    hrz::flat_hash_map<uint32_t, hrz::vector_data::AttributeValues> attributes;
    hrz::flat_hash_map<uint32_t, hrz::vector_data::OwnedAttributeValue> uniforms;
    hrz::flat_hash_map<uint64_t, hrz::vector_data::OwnedAttributeValue> properties_default_values;

    uint64_t rng_seed;
    hrz::BlobArray<hrz::vector_data::FeatureIdHash> feature_ids_hashes;
};

struct StylingResult
{
    hrz::style::StyledFeatures features;

    // List of representations ids that have had at least one instance
    // generated.
    hrz::flat_hash_set<uint32_t> unique_reprs;
};

} // namespace hrz_jobs
