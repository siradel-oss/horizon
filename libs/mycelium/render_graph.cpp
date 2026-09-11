// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "mycelium/render_graph.h"

#include "absl/container/flat_hash_map.h"
#include "internal/intern_string.h"
#include "internal/log.h"
#include "mycelium/properties.h"
#include "mycelium/renderer.h"

#include <assert.h>

#include <algorithm>
#include <stdio.h>
#include <string>
#include <vector>

namespace my
{

static const RenderPassId NO_RENDER_PASS = ~(RenderPassId)0;
static const uint32_t NO_LOGICAL_RESOURCE = ~(uint32_t)0;
static const uint32_t NO_PHYSICAL_RESOURCE = ~(uint32_t)0;

struct NullableCString
{
    const char* str;

    explicit NullableCString(const char* str) : str(str) {}

    explicit NullableCString(uintptr_t ptr) : str((const char*)ptr) {}
};

const char* format_as(NullableCString n)
{
    return n.str ? n.str : "(null)";
}

class RenderGraphImpl : public RenderGraph
{
    static inline bool are_compatible(const ResourceInfo& info1, const ResourceInfo& info2)
    {
        return info1.format == info2.format && info1.size_class == info2.size_class
            && info1.width == info2.width && info1.height == info2.height;
    }

    struct Resource
    {
        RenderPassId producer;
        bool is_read;
        bool is_modified;
        bool is_sampled;
        uint32_t logical;
    };

    struct LogicalResource
    {
        ResourceInfo info;
        uint32_t first_use;
        uint32_t last_use;
        uint32_t physical;
        const char* name;
        bool is_sampled;
    };

    struct PhysicalResource
    {
        ResourceInfo info;
        ResourceHandle handle;
        std::string name;
        bool is_sampled;
    };

    struct Pass
    {
        std::string name;
        RenderPass* pass;

        // There is always the same number of resources in consumed
        // and in produced, but they can be null.
        // null in consumed and non-null in produced = output
        // non-null in consumed and non-null in produced = modified
        // non-null in consumed and null in produced = input
        std::vector<uintptr_t> consumed;
        std::vector<uintptr_t> produced;

        size_t invocations;
    };

    bool _built = false;
    std::vector<Pass> _passes;
    InternString _intern;
    absl::flat_hash_map<uintptr_t, Resource> _resources;
    std::vector<LogicalResource> _logical_resources;
    std::vector<PhysicalResource> _physical_resources;
    std::vector<RenderPassId> _built_order;
    std::vector<std::vector<RenderPassId>> _subset_orders;

    uint32_t _backbuffer_width = 400;
    uint32_t _backbuffer_height = 300;

    void dump_graph() const
    {
        MY_LOG_INFO("===== Dumping the render graph (built = {}) =====", (int)_built);
        for (const Pass& pass : _passes)
        {
            MY_LOG_INFO("Pass \"{}\":", pass.name);
            for (unsigned int i = 0; i < pass.consumed.size(); ++i)
            {
                MY_LOG_INFO(
                    "    - {} -> {}", NullableCString(pass.consumed[i]),
                    NullableCString(pass.produced[i]));
            }
        }

        for (const auto& res : _resources)
        {
            MY_LOG_INFO("Resources \"{}\":", (const char*)res.first);
            MY_LOG_INFO("    - producer = \"{}\"", _passes[res.second.producer].name);
            MY_LOG_INFO("    - logical = {}", (int)res.second.logical);
            if (res.second.logical != NO_LOGICAL_RESOURCE)
            {
                MY_LOG_INFO(
                    "    - physical = {}", (int)_logical_resources[res.second.logical].physical);
            }
        }
        MY_LOG_INFO("=================================================");
    }

    static inline const char* size_class_str(ResourceInfo::SizeClass size_class)
    {
        switch (size_class)
        {
            case ResourceInfo::SizeClass::BackbufferRelative: return "Relative";
            case ResourceInfo::SizeClass::Absolute:
            default: return "Absolute";
        }
    }

    void dump_resources_timeline() const
    {
        printf("%%%% This is is Mermaid graph\n");
        printf("gantt\n");
        printf("    dateFormat X\n");
        printf("    title Render graph physical resources timeline\n");

        for (size_t pres_id = 0; pres_id < _physical_resources.size(); ++pres_id)
        {
            const auto& pres = _physical_resources[pres_id];

            if (pres.info.size_class == ResourceInfo::SizeClass::Absolute)
            {
                printf(
                    "    section res_%d %s %dx%d %s\n", (int)pres_id, format_str(pres.info.format),
                    (int)floor(pres.info.width), (int)floor(pres.info.height),
                    size_class_str(pres.info.size_class));
            }
            else
            {
                printf(
                    "    section res_%d %s, %.3fx%.3f, %s\n", (int)pres_id,
                    format_str(pres.info.format), pres.info.width, pres.info.height,
                    size_class_str(pres.info.size_class));
            }

            for (size_t i = 0; i < _logical_resources.size(); ++i)
            {
                const auto& res = _logical_resources[i];
                if (res.physical == pres_id)
                {
                    printf(
                        "    %d :res%d_%d, %d, %d\n", (int)i, (int)i, (int)pres_id, res.first_use,
                        res.last_use);
                }
            }
        }
    }

    Resource& get_or_create_resource(uintptr_t id)
    {
        auto it = _resources.find(id);
        if (it != _resources.end())
        {
            return it->second;
        }
        else
        {
            Resource& res = _resources[id];
            res.producer = NO_RENDER_PASS;
            res.is_read = false;
            res.is_modified = false;
            res.is_sampled = false;
            res.logical = NO_LOGICAL_RESOURCE;
            return res;
        }
    }

    void link_resource_to_pass(
        RenderPassId pass_id,
        uintptr_t in_id,
        uintptr_t out_id,
        uint32_t logical_resource,
        ResourceUsage usage)
    {
        bool is_sampled = usage & ResourceUsage::Sampled;

        if (out_id)
        {
            Resource& out_resource = get_or_create_resource(out_id);
            out_resource.is_sampled |= is_sampled;

            if (out_resource.producer == NO_RENDER_PASS)
            {
                out_resource.producer = pass_id;
                out_resource.logical = logical_resource;
            }
            else
            {
                MY_LOG_ERROR("Resource '{}' is produced twice", (const char*)out_id);
                return;
            }
        }

        if (in_id)
        {
            Resource& in_resource = get_or_create_resource(in_id);
            in_resource.is_sampled |= is_sampled;

            if (out_id)
            {
                in_resource.is_modified = true;
            }
            else
            {
                in_resource.is_read = true;
            }
        }

        _passes[pass_id].consumed.push_back(in_id);
        _passes[pass_id].produced.push_back(out_id);
    }

    void add_pass_output(
        RenderPassId pass_id,
        const char* name,
        ResourceUsage usage,
        const ResourceInfo& info)
    {
        assert(!_built);
        assert(pass_id < _passes.size());

        uintptr_t res_id = _intern.intern(name);
        uint32_t logical_id = _logical_resources.size();

        LogicalResource logical_res;
        logical_res.info = info;
        logical_res.name = _intern.get_string(res_id);
        logical_res.is_sampled = (usage & ResourceUsage::Sampled) != 0;
        _logical_resources.push_back(logical_res);

        link_resource_to_pass(pass_id, 0, res_id, logical_id, usage);
    }

    void add_pass_input(RenderPassId pass_id, const char* name, ResourceUsage usage)
    {
        assert(!_built);
        assert(pass_id < _passes.size());

        uintptr_t res_id = _intern.intern(name);

        link_resource_to_pass(pass_id, res_id, 0, NO_LOGICAL_RESOURCE, usage);
    }

    void add_pass_inout(
        RenderPassId pass_id,
        const char* in_name,
        ResourceUsage usage,
        const char* out_name)
    {
        assert(!_built);
        assert(pass_id < _passes.size());

        uintptr_t id_in = _intern.intern(in_name);
        uintptr_t id_out = _intern.intern(out_name);

        link_resource_to_pass(pass_id, id_in, id_out, NO_LOGICAL_RESOURCE, usage);
    }

    void set_invocation_count(RenderPassId pass_id, size_t count)
    {
        assert(!_built);
        assert(pass_id < _passes.size());

        _passes[pass_id].invocations = count;
    }

    ResourceHandle retrieve_resource(const char* name) const
    {
        assert(_built);

        uintptr_t id = _intern.intern_or_null(name);
        if (!id)
        {
            MY_LOG_ERROR("Couldn't find resource '{}'", name);
            return ResourceHandle::null();
        }

        auto it = _resources.find(id);
        if (it != _resources.end())
        {
            uint32_t logical = it->second.logical;
            if (logical != NO_LOGICAL_RESOURCE)
            {
                return _physical_resources[_logical_resources[logical].physical].handle;
            }
        }

        return ResourceHandle::null();
    }

    class SetupContextImpl final : public SetupContext
    {
        RenderGraphImpl& _rg;
        RenderPassId _rp;

    public:
        SetupContextImpl(RenderGraphImpl& rg, RenderPassId id) : _rg(rg), _rp(id) {}

        void create(const char* name, ResourceUsage usage, const ResourceInfo& info) override
        {
            _rg.add_pass_output(_rp, name, usage, info);
        }

        void read(const char* name, ResourceUsage usage) override
        {
            _rg.add_pass_input(_rp, name, usage);
        }

        void read_write(const char* in, ResourceUsage usage, const char* out) override
        {
            _rg.add_pass_inout(_rp, in, usage, out);
        }

        void set_invocation_count(size_t count) override { _rg.set_invocation_count(_rp, count); }
    };

    class ResourceContextImpl final : public ResourceContext
    {
        const RenderGraphImpl& _rg;

    public:
        explicit ResourceContextImpl(const RenderGraphImpl& rg) : _rg(rg) {}

        ResourceHandle retrieve(const char* name) const override
        {
            return _rg.retrieve_resource(name);
        }
    };

public:
    RenderPassId add_pass(const char* name, RenderPass* render_pass) override
    {
        if (_built)
        {
            assert(!"Cannot add a pass to a built render graph");
            return ~RenderPassId(0);
        }

        Pass pass;
        pass.pass = render_pass;
        pass.name = name;
        pass.invocations = 1;

        _passes.push_back(pass);
        return _passes.size() - 1;
    }

    RenderPassId find_producer(const char* name) const
    {
        uintptr_t id = _intern.intern_or_null(name);
        auto it = _resources.find(id);
        if (it == _resources.end())
        {
            return NO_RENDER_PASS;
        }
        else
        {
            return it->second.producer;
        }
    }

    bool assign_logical_resources()
    {
        for (auto& it : _resources)
        {
            const Resource& resource = it.second;

            if (resource.logical != NO_LOGICAL_RESOURCE)
            {
                _logical_resources[resource.logical].is_sampled |= resource.is_sampled;
                continue;
            }

            std::vector<uintptr_t> stack;
            uint32_t logical_resource = NO_LOGICAL_RESOURCE;
            uintptr_t visiting = it.first;

            while (true)
            {
                if (stack.size() > _resources.size())
                {
                    MY_LOG_ERROR("Cycle detected!");
                    dump_graph();
                    return false;
                }

                const Resource& visiting_resource = _resources[visiting];
                if (visiting_resource.logical != NO_LOGICAL_RESOURCE)
                {
                    logical_resource = visiting_resource.logical;
                    break;
                }

                RenderPassId producer = visiting_resource.producer;
                if (producer == NO_RENDER_PASS)
                {
                    MY_LOG_ERROR(
                        "Produced resource '{}' has no producer, what?", (const char*)visiting);
                    return false;
                }

                Pass& pass = _passes[producer];
                if (pass.produced.size() != pass.consumed.size())
                {
                    MY_LOG_ERROR("Pass '{}' produced and consumed are not same size", pass.name);
                    return false;
                }

                uintptr_t next_to_visit = 0;
                for (uint32_t i = 0; i < pass.produced.size(); ++i)
                {
                    if (pass.produced[i] == visiting)
                    {
                        next_to_visit = pass.consumed[i];
                        break;
                    }
                }

                if (!next_to_visit)
                {
                    MY_LOG_ERROR(
                        "Couldn't find input resource for resource '{}' in pass '{}'",
                        (const char*)visiting, pass.name);
                    return false;
                }

                stack.push_back(visiting);
                visiting = next_to_visit;
            }

            if (logical_resource == NO_LOGICAL_RESOURCE)
            {
                MY_LOG_ERROR(
                    "Logical resource assignment failed for resource '{}'", (const char*)it.first);
            }

            _logical_resources[logical_resource].is_sampled |= resource.is_sampled;

            for (uintptr_t id : stack)
            {
                _resources[id].logical = logical_resource;
            }
        }

        return true;
    }

    std::vector<uint32_t> build_pass_order(std::span<const RenderPassId> final_passes) const
    {
        // Build passes order
        uint32_t next_order = 0;
        std::vector<uint32_t> order;
        order.resize(_passes.size(), ~0U);

        // This contains all preceding nodes that we're visiting.
        // It's essentially a stack but we don't care about the order, we only
        // care about checking for existence to check for cycles.
        // So that's why it's a set and not a vector.
        absl::flat_hash_set<RenderPassId> visiting_stack;
        std::vector<RenderPassId> to_visit;

        for (const RenderPassId& pass_id : final_passes)
        {
            to_visit.clear();
            visiting_stack.clear();

            to_visit.push_back(pass_id);

            while (!to_visit.empty())
            {
                RenderPassId visiting = to_visit.back();
                const Pass& pass = _passes[visiting];

                visiting_stack.insert(visiting);

                if (order[visiting] == ~0U)
                {
                    bool has_unresolved = false;

                    std::vector<uintptr_t> to_insert_later;

                    for (uintptr_t in_id : pass.consumed)
                    {
                        if (in_id)
                        {
                            auto it = _resources.find(in_id);
                            if (it != _resources.end())
                            {
                                RenderPassId producer = it->second.producer;
                                if (order[producer] == ~0U)
                                {
                                    to_visit.push_back(producer);
                                    has_unresolved = true;

                                    if (visiting_stack.count(producer) > 0)
                                    {
                                        MY_LOG_ERROR("Cycle detected!");
                                        dump_graph();
                                        order.clear();
                                        return order;
                                    }
                                }
                            }
                        }
                    }

                    if (!has_unresolved)
                    {
                        order[visiting] = next_order++;
                    }
                }

                if (order[visiting] != ~0U)
                {
                    size_t erased = visiting_stack.erase(visiting);
                    assert(erased);
                    (void)erased;

                    to_visit.pop_back();
                }
            }
        }

        return order;
    }

    void build_physical_resources(my::ResourceContext* rc)
    {
        for (auto& res : _physical_resources)
        {
            uint32_t width = (uint32_t)res.info.width;
            uint32_t height = (uint32_t)res.info.height;

            if (res.info.size_class == ResourceInfo::BackbufferRelative)
            {
                width = (uint32_t)(res.info.width * _backbuffer_width);
                height = (uint32_t)(res.info.height * _backbuffer_height);
            }

            if (res.is_sampled)
            {
                TextureResource tex;
                tex.layout.type = TextureLayout::Type2D;
                tex.layout.format = res.info.format;
                tex.layout.width = width;
                tex.layout.height = height;
                tex.layout.depth = 1;
                tex.layout.levels = 1;

                tex.data = {};
                tex.generate_mipmaps = false;

                tex.is_render_graph_texture = true;
                tex.name = res.name.c_str();

                res.handle = rc->alloc(&tex);
            }
            else
            {
                RenderbufferResource rb;
                rb.format = res.info.format;
                rb.width = width;
                rb.height = height;
                rb.is_render_graph_render_buffer = true;
                rb.name = res.name.c_str();
                res.handle = rc->alloc(&rb);
            }
        }
    }

    void assign_logical_resources_use_bounds()
    {
        for (LogicalResource& res : _logical_resources)
        {
            res.first_use = 0xffffffff;
            res.last_use = 0;
            res.physical = NO_PHYSICAL_RESOURCE;
        }

        auto assign_bounds = [&](uintptr_t res_id, uint32_t order)
        {
            LogicalResource& res = _logical_resources[_resources[res_id].logical];
            if (res.first_use > order) res.first_use = order;
            if (res.last_use < order) res.last_use = order;
        };

        for (uint32_t order = 0; order < _built_order.size(); ++order)
        {
            const Pass& pass = _passes[_built_order[order]];
            for (uint32_t i = 0; i < pass.consumed.size(); ++i)
            {
                uintptr_t in = pass.consumed[i];
                uintptr_t out = pass.produced[i];

                if (in)
                {
                    assign_bounds(in, order);
                }

                if (out)
                {
                    assign_bounds(out, order);
                }
            }
        }
    }

    void assign_physical_resources()
    {
        _physical_resources.clear();

        // Stores logical resources ids sorted by their first use.
        std::vector<uint32_t> logical_resources_sorted;
        logical_resources_sorted.reserve(_logical_resources.size());
        for (uint32_t id = 0; id < _logical_resources.size(); ++id)
        {
            logical_resources_sorted.push_back(id);
        }

        std::ranges::sort(
            logical_resources_sorted, [&](uint32_t a, uint32_t b)
            { return _logical_resources[a].first_use < _logical_resources[b].first_use; });

        for (const uint32_t id : logical_resources_sorted)
        {
            const LogicalResource& logical = _logical_resources[id];

            if (logical.physical != NO_PHYSICAL_RESOURCE) continue;

            uint32_t available_from = 0;
            const auto phys_id = (uint32_t)_physical_resources.size();

            std::vector<const char*> names = {logical.name};

            for (const uint32_t res_id : logical_resources_sorted)
            {
                LogicalResource& res = _logical_resources[res_id];
                if (res.physical == NO_PHYSICAL_RESOURCE && res.first_use >= available_from
                    && are_compatible(res.info, logical.info)
                    && res.is_sampled == logical.is_sampled)
                {
                    res.physical = phys_id;
                    available_from = res.last_use + 1;

                    bool name_already_present = false;
                    for (auto name : names)
                    {
                        if (std::strcmp(logical.name, name) == 0)
                        {
                            name_already_present = true;
                            break;
                        }
                    }
                    if (!name_already_present)
                    {
                        names.push_back(logical.name);
                    }
                }
            }

            std::string res_name = "";
            for (auto name : names)
            {
                if (!res_name.empty()) res_name += ", ";
                res_name += name;
            }

            PhysicalResource resource;
            resource.info = logical.info;
            resource.handle = ResourceHandle::null();
            resource.name = res_name;
            resource.is_sampled = logical.is_sampled;
            _physical_resources.push_back(resource);
        }
    }

    bool build(
        my::Instance* instance,
        my::ResourceContext* rc,
        std::span<const RenderPassId> final_passes) override
    {
        if (_built)
        {
            assert(!"Cannot build a render graph twice");
            return false;
        }

        size_t passes_invocations_total = 0;

        for (RenderPassId id = 0; id < _passes.size(); ++id)
        {
            SetupContextImpl ctx(*this, id);
            _passes[id].pass->setup_pass(ctx);
            passes_invocations_total += _passes[id].invocations;
        }

        if (passes_invocations_total > MaxRenderGraphPasses)
        {
            MY_LOG_ERROR("Too many render graph passes invocations");
            return false;
        }

        for (const RenderPassId& pass_id : final_passes)
        {
            if (pass_id >= _passes.size())
            {
                MY_LOG_ERROR("Unknown final render pass");
                return false;
            }
        }

        if (!assign_logical_resources())
        {
            MY_LOG_ERROR("Cannot assign logical resources, a cycle was probably found.");
            return false;
        }

        for (const auto& it : _resources)
        {
            if (it.second.is_read && it.second.is_modified)
            {
                MY_LOG_ERROR(
                    "Resource '{}' is both read and modified. This is not allowed.",
                    (const char*)it.first);
                return false;
            }
        }

        std::vector<uint32_t> order = build_pass_order(final_passes);

        if (order.size() == 0)
        {
            MY_LOG_ERROR("Error building passes order");
            return false;
        }

        _built_order.clear();
        _built_order.resize(order.size());

        uint32_t max_order = 0;

        for (RenderPassId id = 0; id < _passes.size(); ++id)
        {
            if (order[id] < _passes.size())
            {
                uint32_t o = order[id];

                _built_order[o] = id;
                if (o > max_order)
                {
                    max_order = o;
                }
            }
        }

        _built_order.resize(max_order + 1);

        assign_logical_resources_use_bounds();
        assign_physical_resources();
        // dump_resources_timeline();
        build_physical_resources(rc);

        _built = true;

        for (const auto& pass : _passes)
        {
            ResourceContextImpl ctx(*this);
            pass.pass->retrieve_resources(instance, rc, ctx);
        }

        return true;
    }

    RenderGraphSubsetId make_subset(
        my::Instance* instance,
        std::span<const RenderPassId> final_passes) override
    {
        if (!_built)
        {
            assert(!"Cannot make subset before the render graph is built");
            return SubsetError;
        }

        for (const RenderPassId& pass_id : final_passes)
        {
            if (pass_id >= _passes.size())
            {
                MY_LOG_ERROR("Unknown final render pass");
                return SubsetError;
            }
        }

        std::vector<uint32_t> order = build_pass_order(final_passes);

        if (order.size() == 0)
        {
            MY_LOG_ERROR("Error building passes order");
            return false;
        }

        std::vector<RenderPassId> subset_order(order.size());

        uint32_t max_order = 0;

        for (RenderPassId id = 0; id < _passes.size(); ++id)
        {
            if (order[id] < _passes.size())
            {
                uint32_t o = order[id];

                subset_order[o] = id;
                if (o > max_order)
                {
                    max_order = o;
                }
            }
        }

        subset_order.resize(max_order + 1);

        RenderGraphSubsetId subset_id = _subset_orders.size();
        _subset_orders.push_back(subset_order);

        return subset_id;
    }

    void resize_resources(my::ResourceContext* rc) const
    {
        for (auto& res : _physical_resources)
        {
            if (res.info.size_class != ResourceInfo::BackbufferRelative) continue;

            uint32_t width = (uint32_t)(res.info.width * _backbuffer_width);
            uint32_t height = (uint32_t)(res.info.height * _backbuffer_height);

            if (res.is_sampled)
            {
                TextureLayout layout;
                layout.type = TextureLayout::Type2D;
                layout.format = res.info.format;
                layout.width = width;
                layout.height = height;
                layout.depth = 1;
                layout.levels = 1;

                rc->update_texture_layout(res.handle, layout);
            }
            else
            {
                rc->update_renderbuffer_size(res.handle, width, height);
            }
        }
    }

    void execute_with_order(const ExecutionContext& ctx_in, const std::vector<RenderPassId>& order)
    {
        if (!_built)
        {
            assert(!"Cannot execute before the render graph is built");
            return;
        }

        if (order.size() > 64)
        {
            MY_LOG_ERROR("Too many passes, stuff will break down from now on.");
        }

        ExecutionContext ctx = ctx_in;

        if (ctx.backbuffer_width != _backbuffer_width
            || ctx.backbuffer_height != _backbuffer_height)
        {
            _backbuffer_width = ctx.backbuffer_width;
            _backbuffer_height = ctx.backbuffer_height;
            resize_resources(ctx.resource);
        }

        for (RenderPassId pass_id : order)
        {
            size_t invocations = _passes[pass_id].invocations;
            for (size_t invocation = 0; invocation < invocations; ++invocation)
            {
                ctx.invocation = invocation;
                _passes[pass_id].pass->execute(ctx);
            }
        }
    }

    void execute(const ExecutionContext& ctx_in) override
    {
        execute_with_order(ctx_in, _built_order);
    }

    void execute_subset(const ExecutionContext& ctx_in, RenderGraphSubsetId subset_id) override
    {
        if (subset_id >= _subset_orders.size())
        {
            MY_LOG_ERROR("Invalid render graph subset ID: {}", subset_id);
            return;
        }

        execute_with_order(ctx_in, _subset_orders.at(subset_id));
    }

    void free(my::ResourceContext* rc) override
    {
        for (auto& res : _physical_resources)
        {
            rc->dealloc(res.handle);
        }
        _built = false;
    }
};

RenderGraph* RenderGraph::create()
{
    return new RenderGraphImpl();
}

} // namespace my
