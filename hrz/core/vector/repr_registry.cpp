#include "hrz/common/profiling.h"
#include "hrz/core/global_flags.h"
#include "hrz/core/render.h"
#include "hrz/core/vector/repr.h"
#include "hrz/fnd/thread.h"

namespace
{
class NullReprSystem : public hrz::vt::ReprSystem
{
public:
    ~NullReprSystem() override = default;

    void deinit(WorkCtx&, hrz::Render*) override {}

    void init_render(hrz::Render* render) override {}

    void deinit_render(hrz::Render* render) override {}

    void schedule_draw_now(uint64_t channel_id, uint64_t tile_id, uint32_t views_bitset) override {}

    hrz::RenderRequest work(WorkCtx&) override { return {}; }

    hrz::RenderRequest work_gpu(WorkGpuCtx&) override { return {}; }

    void draw(DrawCtx&) override {}

    std::pair<uint64_t, Channel> create_channel() override
    {
        return {0, hrz::create_channel<hrz::vt::ToReprMessage, hrz::vt::FromReprMessage>().first};
    }
};

} // namespace

namespace hrz::vt
{
std::unique_ptr<ReprSystem> create_null_repr_system()
{
    return std::unique_ptr<ReprSystem>(new NullReprSystem());
}

void ReprRegistry::destroy(
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js,
    HeatmapReprRegistry* heatreg,
    ImageLoader* il,
    FontRasterizer* fr,
    SymbolCullingSystem* symbol_culling,
    Render* render)
{
    assert(al && ba && js && heatreg && il && fr && symbol_culling);

    ReprSystem::WorkCtx ctx;
    ctx.al = al;
    ctx.ba = ba;
    ctx.js = js;
    ctx.fr = fr;
    ctx.heatreg = heatreg;
    ctx.il = il;
    ctx.repr_reg = this;
    ctx.symbol_culling = symbol_culling;
    ctx.views_info = {};

    for (auto& repr : _reprs)
    {
        repr.second->deinit(ctx, render);
    }
}

uint64_t ReprRegistry::register_property(uint64_t layer_id, std::string_view name)
{
    if (name.empty()) return hrz::style::Parser::INVALID_PROPERTY;

    // Note that properties in different layers are independent but duplicated property names are
    // interned only once. Thus, different properties with the same name have the same ID. This is
    // not an issue in practice though since properties are stored per layer and always worked with
    // per layer as well.
    uintptr_t intern = (uintptr_t)_intern.intern(name);
    uint64_t prp_id = (uint64_t)intern;

    auto& prps = _layer_ids_to_prps[layer_id];
    auto it = prps.find(intern);
    if (it != prps.end())
    {
        it->second.ref_count++;
        return prp_id;
    }
    else
    {
        Property prp{1};
        prps.insert(std::make_pair(intern, prp));
        return prp_id;
    }
}

void ReprRegistry::unregister_property(uint64_t layer_id, uint64_t prp_id)
{
    if (prp_id == hrz::style::Parser::INVALID_PROPERTY) return;

    auto it = _layer_ids_to_prps.find(layer_id);
    if (it == _layer_ids_to_prps.end()) return;

    auto& prps = it->second;
    auto prp_it = prps.find((uintptr_t)prp_id);
    if (prp_it != prps.end())
    {
        prp_it->second.ref_count--;
        if (prp_it->second.ref_count == 0)
        {
            prps.erase(prp_it);
        }
    }

    if (prps.size() == 0)
    {
        _layer_ids_to_prps.erase(it);
    }
}

void ReprRegistry::register_repr(hrz_proto::VectorReprType type, std::unique_ptr<ReprSystem> repr)
{
    _reprs.insert(std::make_pair(type, std::move(repr)));
}

void ReprRegistry::register_properties(uint64_t layer_id, hrz::style::Parser& parser)
{
    const auto& prps = _layer_ids_to_prps[layer_id];
    for (const auto& prp : prps)
    {
        parser.add_property((uint64_t)prp.first, (const char*)prp.first);
    }
}

void ReprRegistry::init_render(Render* render)
{
    assert(render);
    for (auto& repr : _reprs)
    {
        repr.second->init_render(render);
    }
}

void ReprRegistry::deinit_render(Render* render)
{
    assert(render);
    for (auto& repr : _reprs)
    {
        repr.second->deinit_render(render);
    }
}

RenderRequest ReprRegistry::work(
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js,
    HeatmapReprRegistry* heatreg,
    ImageDecoder* imgdec,
    ImageLoader* il,
    FontRasterizer* fr,
    SymbolCullingSystem* symbol_culling,
    AttributionRegistry* attributions,
    std::span<const RenderViewInfo> views_info)
{
    assert(al && ba && js && heatreg && imgdec && il && fr && symbol_culling);

    ReprSystem::WorkCtx ctx;
    ctx.al = al;
    ctx.ba = ba;
    ctx.js = js;
    ctx.imgdec = imgdec;
    ctx.il = il;
    ctx.fr = fr;
    ctx.heatreg = heatreg;
    ctx.repr_reg = this;
    ctx.symbol_culling = symbol_culling;
    ctx.attributions = attributions;
    ctx.views_info = views_info;

    RenderRequest render_request;

    for (auto& repr : _reprs)
    {
        render_request |= repr.second->work(ctx);
    }

    return render_request;
}

RenderRequest ReprRegistry::work_gpu(
    Render* render,
    BlobAllocator* ba,
    ImageLoader* il,
    SymbolCullingSystem* culling)
{
    HRZ_SCOPED_SAMPLE("repr registry work gpu");

    assert(render);

    RenderRequest render_request;

    if (hrz::get_flag(hrz::Flag::DebugFreezeVectorTilesCulling))
    {
        return render_request;
    }

    ReprSystem::WorkGpuCtx ctx;
    ctx.render = render;
    ctx.ba = ba;
    ctx.il = il;
    ctx.symbol_culling = culling;

    for (auto& repr : _reprs)
    {
        render_request |= repr.second->work_gpu(ctx);
    }

    return render_request;
}

void ReprRegistry::draw(Render* render, AttributionRegistry* attributions)
{
    assert(render);

    ReprSystem::DrawCtx ctx;
    ctx.render = render;
    ctx.attributions = attributions;

    for (auto& repr : _reprs)
    {
        repr.second->draw(ctx);
    }
}

ReprSystem& ReprRegistry::get(hrz_proto::VectorReprType type)
{
    auto it = _reprs.find(type);
    if (it != _reprs.end())
    {
        return *it->second;
    }
    else
    {
        HRZ_LOG_WARNING(
            "Fetching unknown representation type {}", hrz_proto::VectorReprType_Name(type));
        assert(!"Representation system has not been registered yet");
        return *_reprs[hrz_proto::VectorReprType::NULL_VECTOR_REPR];
    }
}

uint64_t ReprSystem::next_displayable_frame() const
{
    return hrz::Render::CurrentFrame;
}
} // namespace hrz::vt
