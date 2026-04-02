#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace hrz
{

struct AttributionRegistry;

struct AttributionHandle
{
    uintptr_t o{};

    constexpr operator bool() const { return o != 0; }

    constexpr auto operator <=>(const AttributionHandle& other) const = default;
};

template<typename H>
H AbslHashValue(H h, AttributionHandle attribution)
{
    return H::combine(std::move(h), attribution.o);
}

struct Attribution
{
    std::string_view title;
    std::string_view logo;
};

namespace attribution
{

AttributionRegistry* create_registry();
void destroy(AttributionRegistry*);

AttributionHandle register_attribution(AttributionRegistry*, const Attribution&);

// Warning: do not use this to register runtime groups as you might have a lot of them, and they are
// never deleted. This should only be used to register groups of attributions that will always be
// used together.
AttributionHandle register_attribution_group(
    AttributionRegistry*,
    std::span<const AttributionHandle>);

void use_this_frame(AttributionRegistry*, AttributionHandle);
void use_this_frame(AttributionRegistry*, std::span<const AttributionHandle>);

void reset_used_attributions(AttributionRegistry*);

// Use the result before any other method is called.
// The std::string_views in the Attribution objects are guaranteed to be stable until the registry
// is destroyed. However the array itself might not.
std::span<const Attribution> get_frame_attributions(const AttributionRegistry*);

} // namespace attribution
} // namespace hrz
