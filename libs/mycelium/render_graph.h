#pragma once

#include "mycelium/backend.h"

namespace my
{
class ResourceBinder;
class Renderer;
class RenderPass;

using RenderPassId = uint32_t;
using RenderGraphSubsetId = uint32_t;

class RenderGraph
{
public:
    enum ResourceUsage
    {
        Target = 1,
        Sampled = 2,
        TargetSampled = Target | Sampled,
    };

    struct ResourceInfo
    {
        enum SizeClass
        {
            Absolute,
            BackbufferRelative,
        };

        TextureFormat format;
        SizeClass size_class;
        float width;
        float height;
    };

    class SetupContext
    {
    public:
        virtual ~SetupContext() = default;

        virtual void set_invocation_count(size_t count) = 0;
        virtual void create(const char* name, ResourceUsage usage, const ResourceInfo&) = 0;
        virtual void read(const char* name, ResourceUsage usage) = 0;
        virtual void read_write(const char* in, ResourceUsage usage, const char* out) = 0;
    };

    class ResourceContext
    {
    public:
        virtual ~ResourceContext() = default;

        virtual ResourceHandle retrieve(const char* name) const = 0;
    };

    struct ExecutionContext
    {
        uint32_t backbuffer_width;
        uint32_t backbuffer_height;

        my::Instance* instance;
        my::ResourceContext* resource;
        my::RenderContext* render;
        my::ResourceBinder* binder;
        my::Renderer* renderer;

        const void* user_data;

        size_t invocation;
    };

    static constexpr RenderGraphSubsetId SubsetError = ~0u;

    static RenderGraph* create();

    virtual ~RenderGraph() = default;

    virtual RenderPassId add_pass(const char* name, RenderPass*) = 0;

    virtual bool build(
        my::Instance*,
        my::ResourceContext*,
        uint32_t final_pass_count,
        const RenderPassId* final_passes) = 0;

    virtual RenderGraphSubsetId make_subset(
        my::Instance*,
        uint32_t final_pass_count,
        const RenderPassId* final_passes) = 0;

    virtual void execute(const ExecutionContext&) = 0;

    virtual void execute_subset(const ExecutionContext&, RenderGraphSubsetId) = 0;

    virtual void free(my::ResourceContext*) = 0;
};

class RenderPass
{
public:
    virtual ~RenderPass() = default;

    virtual void setup_pass(RenderGraph::SetupContext&) = 0;

    virtual void retrieve_resources(
        Instance*,
        ResourceContext*,
        const RenderGraph::ResourceContext&) = 0;

    virtual void execute(const RenderGraph::ExecutionContext&) = 0;
};

} // namespace my
