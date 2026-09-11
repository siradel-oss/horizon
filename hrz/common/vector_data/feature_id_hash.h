// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <lin_maths.h>

#include <bit>
#include <cstdint>

namespace hrz::vector_data
{

using FeatureIdHash = uint64_t;

inline lm::uvec2 feature_id_hash_as_uvec2(FeatureIdHash hash)
{
    static_assert(
        sizeof(vector_data::FeatureIdHash) == sizeof(uint64_t), "Unsupported feature ID hash size");
    static_assert(
        std::endian::native == std::endian::little,
        "This operation relies on hashes being sent as little-endian values to the GPU");

    return std::bit_cast<lm::uvec2>(hash);
}

} // namespace hrz::vector_data
