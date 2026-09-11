// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/metadata.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/core/monitoring/gpu.h"

#include <mycelium/backend.h>

namespace hrz
{

struct GpuResourceContext : public my::ResourceContext
{
public:
    my::ResourceHandle alloc(const my::Resource* res) override;
    my::ResourceHandle alloc(
        const my::Resource* res,
        monitoring::systems::Name system,
        uint64_t layer_id = 0,
        std::initializer_list<std::pair<MetadataString, MetadataString>> metadata = {});
    my::ResourceHandle alloc(
        const my::Resource* res,
        const monitoring::ResourceOwner& resource_owner,
        std::initializer_list<std::pair<MetadataString, MetadataString>> metadata = {});
    my::ResourceHandle alloc(
        const my::Resource* res,
        const monitoring::ResourceOwner& resource_owner,
        std::span<std::pair<MetadataString, MetadataString>> metadata);

    void dealloc(my::ResourceHandle handle) override { rc->dealloc(handle); }

    void realloc_buffer(my::ResourceHandle handle, const my::BufferResource* res) override
    {
        rc->realloc_buffer(handle, res);
    }

    void update_texture_layout(my::ResourceHandle handle, const my::TextureLayout& layout) override
    {
        rc->update_texture_layout(handle, layout);
    }

    void update_renderbuffer_size(my::ResourceHandle handle, uint32_t width, uint32_t height)
        override
    {
        rc->update_renderbuffer_size(handle, width, height);
    }

    my::ResourceHandle retrieve_shader(const char* name) const override
    {
        return rc->retrieve_shader(name);
    }

    my::ResourceContext* rc;
    monitoring::GpuResourceMonitoring* monitoring;

    monitoring::systems::Name default_system_for_allocs = monitoring::systems::NoSystem;

private:
    void register_texture_metadata(const my::Resource* res, my::ResourceHandle handle);
};

} // namespace hrz
