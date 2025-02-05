#pragma once

#include <gsl/gsl-lite.hpp>

namespace hrz::migration
{

struct DescriptorSetEntry
{
    uint32_t id;
    gsl::span<const std::byte> descriptor_set;
};

static constexpr size_t DescriptorSetCount = {{ descriptor_sets | count }};
extern DescriptorSetEntry DescriptorSets[DescriptorSetCount];

void initialize_descriptor_sets();

} // namespace hrz
