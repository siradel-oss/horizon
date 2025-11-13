#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace hrz::migration
{

struct DescriptorSetEntry
{
    uint32_t id;
    std::span<const std::byte> descriptor_set;
};

static constexpr size_t DescriptorSetCount = {{ descriptor_sets | count }};
extern DescriptorSetEntry DescriptorSets[DescriptorSetCount];

void initialize_descriptor_sets();

} // namespace hrz
