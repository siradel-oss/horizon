#include "hrz_core_channel_group.h"
#include "hrz_core_render.h"
#include "hrz_core_selection_storage.h"
#include "vector/hrz_core_vector_repr.h"
#include "vector/symbol/hrz_core_vector_symbol_anchor.h"
#include "vector/symbol/hrz_core_vector_symbol_decorated_shape.h"
#include "vector/symbol/hrz_core_vector_symbol_element.h"
#include "vector/symbol/hrz_core_vector_symbol_image.h"
#include "vector/symbol/hrz_core_vector_symbol_layout.h"
#include "vector/symbol/hrz_core_vector_symbol_leader_line.h"
#include "vector/symbol/hrz_core_vector_symbol_placeholder.h"
#include "vector/symbol/hrz_core_vector_symbol_text.h"

#include <hrz_common_blob_array.h>
#include <hrz_common_fmt.h>
#include <hrz_common_profiling.h>
#include <hrz_common_vector_tiles.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_maths.h>
#include <hrz_fnd_meta.h>
#include <hrz_fnd_thread.h>
#include <hrz_jobs_tickets.h>
#include <hrz_protocol_all.h>

#include <lin_maths.h>

#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace hrz::vt
{
namespace symbol
{
namespace
{
using ConfigH = uint64_t;
using TileH = uint64_t;

struct Config
{
    enum class Status
    {
        Loading,
        Ready,
        Error,
    };

    Status status = Status::Error;
    uint32_t repr_id = 0;
    uint64_t layer_id = 0;

    // Index among representations.
    // Only the least significant 16 bits are usable here,
    // to sort between representations.
    // (8 bits are used to sort between elements of the same
    // symbol representation and a further 8 bits are used to
    // sort between draw calls of a single element, summing
    // up to 32 bits.)
    uint32_t z_index = 0;

    bool clip_to_tile = false;
    bool ignore_occlusions = false;

    uint32_t scene_views = 0;

    // Each instance of `Element` correspond to one element
    // in the scene descriptor. If the same element type
    // appears multiple times in the descriptor, multiple
    // instances of `Element` are created. (This is necessary
    // because they have different indices, and can have
    // different configurations, leading to different data
    // in the prototypes.)
    // Every visual element is placed in the scene by refer-
    // ring to its anchor, so elements store the index to the
    // closest anchor among their ancestors.
    struct Element
    {
        struct AnchorIndex
        {
            // Index among all elements
            uint32_t element_index;

            // Index among anchors
            uint32_t anchor_index;
        };

        ElementSystem::PrototypeH prototype;

        // Index among all elements
        std::optional<uint32_t> parent_index;

        // For anchors, the index refers to themselves.
        std::optional<AnchorIndex> anchor_index;

        // Index among visual elements
        std::optional<uint32_t> z_index;

        SymbolBakingData::ElementBakingParams baking_params;
    };

    // The elements are kept in the order they are in the scene
    // descriptor, but the tree is flattened in a depth-first
    // order.
    // This corresponds to how elements are stacked on top of
    // one another, and hence their z-index. (Except that non-
    // visual elements do not have a z-index.)
    // Links between parents and childrens are maintained by
    // children, who store the index of their parent in this
    // array.
    std::vector<Element> elements;

    // This is data specific to anchors that will be reused by the culling system. Anchors are in
    // the same order as in `element`, but this only contains the anchors.
    hrz::InlinedVector<vt::AnchorPrototype, 8> anchors;
};

struct TileId
{
    uint64_t channel_id;
    uint64_t tile_id;

    bool operator==(const TileId& other) const
    {
        return other.channel_id == channel_id && other.tile_id == tile_id;
    }

    template<typename H>
    friend H AbslHashValue(H h, const TileId& request)
    {
        return H::combine(std::move(h), request.channel_id, request.tile_id);
    }
};

struct Tile
{
    enum class Status
    {
        AwaitingConfig,
        Baking,
        Baked,
        Renderable,
        Error
    };

    std::optional<TileId> id;
    Status status;
    ConfigH config;
    uint64_t layer_id;
    TileCoords coords;
    picking::ObjectReference object_ref;
    picking::FeatureReference feature_ref;
    bool has_feature_ids;

    std::optional<hrz::vt::SymbolBakingData> baking_data;
    std::optional<hrz::vt::BakedSymbols> baked_data;

    hrz_jobs::BakeSymbolsTicket baking_ticket;

    lm::dvec3 center;
    double bsphere_radius;
    lm::dvec3 bsphere_center;

    my::ResourceHandle ubo = my::ResourceHandle::null();
    my::ResourceHandle anchor_data_texture = my::ResourceHandle::null();

    selection::SelectionStorageUint32TextureMultiIndex selection_storage;

    // One entry in this array refers to the data used to
    // draw all instances of one element in the tile.
    // Only visual elements have entries.
    // They are indexed by their z-indices.
    std::vector<ElementSystem::RenderableH> renderables;

    std::optional<symbol_culling::GroupHandle> culling_group;

    uint32_t scene_views;
};

class SymbolReprSystem : public ReprSystem
{
private:
    using ConfigIndexPool = hrz::GenIndexPool<ConfigH, 32, 32>;
    using ConfigPool = hrz::GenObjectPool<Config, ConfigIndexPool, 64>;

    using TileIndexPool = hrz::GenIndexPool<TileH, 32, 32>;
    using TilePool = hrz::GenObjectPool<Tile, TileIndexPool, 64>;

    hrz::flat_hash_map<hrz_proto::SymbolElementType, std::unique_ptr<ElementSystem>>
        _element_systems;
    AnchorElementSystem* _anchor_element_system;

    ConfigPool _configs;

    struct ConfigId
    {
        uint64_t channel_id;
        uint64_t config_id;

        bool operator==(const ConfigId& other) const
        {
            return other.channel_id == channel_id && other.config_id == config_id;
        }

        template<typename H>
        friend H AbslHashValue(H h, const ConfigId& request)
        {
            return H::combine(std::move(h), request.channel_id, request.config_id);
        }
    };

    hrz::flat_hash_map<ConfigId, ConfigH> _configs_by_id;

    TilePool _tiles;

    hrz::flat_hash_map<TileId, TileH> _tiles_by_id;

    hrz::flat_hash_set<ConfigH> _loading_configs;
    std::vector<ConfigH> _unregistered_configs;

    hrz::flat_hash_set<TileH> _loading_tiles;
    hrz::flat_hash_set<TileH> _renderable_tiles;
    hrz::flat_hash_set<TileH> _removed_tiles;

    std::vector<std::pair<TileH, uint32_t>> _tiles_to_render;

    my::ResourceHandle _data_texture_sampler = my::ResourceHandle::null();

    std::vector<my::ResourceHandle> _unused_resources;

    uint64_t _next_displayable_frame = 0;

    hrz::ChannelGroup<hrz::vt::FromReprMessage, hrz::vt::ToReprMessage> _channels;

public:
    SymbolReprSystem()
    {
        auto anchor_element_system = std::make_unique<AnchorElementSystem>();
        _anchor_element_system = anchor_element_system.get();

        _element_systems.insert(
            {hrz_proto::SymbolElementType::PLACEHOLDER_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new PlaceholderElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(std::move(anchor_element_system))});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::STACK_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new StackElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::IMAGE_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new ImageElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::PADDING_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new PaddingElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::SIZED_BOX_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new SizedBoxElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::FLEX_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new FlexElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::FLEXIBLE_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new FlexibleElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::STACK_EXPAND_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new StackExpandElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::ALIGN_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new AlignElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::CONSTRAINED_BOX_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new ConstrainedBoxElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::ROTATED_BOX_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new RotatedBoxElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::DECORATED_SHAPE_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new DecoratedShapeElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::ASPECT_RATIO_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new AspectRatioElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::FITTED_BOX_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new FittedBoxElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::TRANSFORM_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new TransformElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::TEXT_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new TextElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::LEADER_LINE_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new LeaderLineElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::OPTIONAL_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new OptionalElementSystem())});
        _element_systems.insert(
            {hrz_proto::SymbolElementType::VARIANT_SYMBOL_ELEMENT,
             std::unique_ptr<ElementSystem>(new VariantElementSystem())});
    }

    ~SymbolReprSystem() override = default;

    void deinit(WorkCtx& ctx, Render* render) override
    {
        for (auto tile_handle : _loading_tiles)
        {
            _removed_tiles.insert(tile_handle);
        }
        _loading_tiles.clear();

        for (auto tile_handle : _renderable_tiles)
        {
            _removed_tiles.insert(tile_handle);
        }
        _renderable_tiles.clear();

        for (auto config_handle : _loading_configs)
        {
            _unregistered_configs.push_back(config_handle);
        }
        _unregistered_configs.clear();

        work_removed_configs_and_tiles(ctx);

        for (auto resource : _unused_resources)
        {
            render->rc->dealloc(resource);
        }
        _unused_resources.clear();

        for (auto& it : _element_systems)
        {
            it.second->deinit(ctx, render);
        }
    }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        PlaceholderElementSystem::collect_shaders(rc);
        ImageElementSystem::collect_shaders(rc);
        DecoratedShapeElementSystem::collect_shaders(rc);
        TextElementSystem::collect_shaders(rc);
        LeaderLineElementSystem::collect_shaders(rc);
    }

    void init_render(Render* render) override
    {
        {
            my::SamplerResource res;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
            res.use_mipmaps = false;

            _data_texture_sampler = render->rc->alloc(&res, hrz::monitoring::systems::Symbols);
        }

        for (auto& it : _element_systems)
        {
            it.second->init_render(render);
        }
    }

    void deinit_render(Render* render) override
    {
        for (auto& it : _element_systems)
        {
            it.second->deinit_render(render);
        }

        for (auto resource : _unused_resources)
        {
            render->rc->dealloc(resource);
        }
        _unused_resources.clear();

        render->rc->dealloc(_data_texture_sampler);
    }

    std::optional<ConfigH> register_style(
        const hrz_proto::VectorRepr& repr,
        uint64_t layer_id,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        ConfigId style_id)
    {
        if (repr.type() != hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR)
        {
            HRZ_LOG_ERROR("Unexpected vector representation type, expected symbol.");
            return std::nullopt;
        }

        if (_configs_by_id.contains(style_id))
        {
            HRZ_LOG_ERROR(
                "Cannot register style: ID {}-{} already in use", style_id.channel_id,
                style_id.config_id);
            return std::nullopt;
        }

        auto config_handle = _configs.alloc(Config{});
        auto config = _configs.get_object(config_handle);

        config->repr_id = repr.id();
        config->layer_id = layer_id;
        config->z_index = (uint32_t)hrz::clamp_cast<uint32_t, uint16_t>(repr.symbol().z_index());
        config->clip_to_tile = repr.symbol().clip_to_tile();
        config->ignore_occlusions = repr.symbol().ignore_world_occlusions();
        config->scene_views = repr.scene_views().bits();

        make_element_prototypes(config, repr.symbol().root_element(), register_prp);

        config->status = Config::Status::Loading;

        _configs_by_id.insert({style_id, config_handle});

        _loading_configs.insert(config_handle);

        return {config_handle};
    }

    void unregister_style(
        ConfigH config_handle,
        const std::function<void(uint64_t prp_id)>& unregister_property)
    {
        auto config = _configs.get_object(config_handle);
        if (!config) return;

        for (auto& element : config->elements)
        {
            auto& system = _element_systems.at(element.prototype.type);
            system->unregister_properties(element.prototype, unregister_property);
        }

        _loading_configs.erase(config_handle);
        _unregistered_configs.push_back(config_handle);
    }

    std::optional<TileH> add_tile(
        ConfigH config_handle,
        TileCoords coords,
        uint64_t layer_id,
        const hrz::picking::ObjectReference& object_ref,
        const hrz::picking::FeatureReference& feature_ref,
        const hrz::vector_data::FeatureIds& feature_ids,
        const ReprGeometry& geometry,
        const style::StyledFeatures& style,
        TileId tile_id)
    {
        if (_tiles_by_id.contains(tile_id))
        {
            HRZ_LOG_ERROR(
                "Cannot add tile: ID {}-{} already in use", tile_id.channel_id, tile_id.tile_id);
            return std::nullopt;
        }

        Config config;
        Config* config_src = _configs.get_object(config_handle);
        if (config_src)
        {
            config = *config_src;
        }
        else
        {
            HRZ_LOG_WARNING("Unknown symbol config, using default values.");
        }

        Tile tile;
        tile.id = tile_id;
        tile.layer_id = layer_id;
        tile.coords = coords;
        tile.status = Tile::Status::AwaitingConfig;
        tile.config = config_handle;
        tile.object_ref = object_ref;
        tile.feature_ref = feature_ref;
        tile.has_feature_ids = feature_ids.has_any_attribute();
        tile.scene_views = config.scene_views;

        tile.baking_data = {hrz::vt::SymbolBakingData{}};
        auto& baking_data = tile.baking_data.value();

        baking_data.tile_coords = tile.coords;
        baking_data.repr_id = config.repr_id;
        baking_data.feature_ids = feature_ids.hashes();
        baking_data.geometry = geometry.geometry;

        baking_data.clamping.CopyFrom(geometry.clamping);
        baking_data.clamps = geometry.clamps;

        baking_data.clip_to_tile = config.clip_to_tile;

        auto& dst_style = baking_data.style;
        dst_style.prps = style.prps;
        dst_style.values = style.values;
        dst_style.out_of_line_data = style.out_of_line_data;
        dst_style.instances = style.instances;

        TileH handle = _tiles.alloc(std::move(tile));

        _tiles_by_id.insert({tile_id, handle});

        _loading_tiles.insert(handle);

        return handle;
    }

    bool schedule_draw_soon(
        uint64_t channel_id,
        uint64_t tile_id,
        uint32_t views_bitset,
        SymbolCullingSystem* culling) override
    {
        auto it = _tiles_by_id.find(TileId{channel_id, tile_id});
        if (it != _tiles_by_id.end())
        {
            Tile* tile = _tiles.get_object(it->second);

            if (tile && tile->culling_group)
            {
                symbol_culling::schedule_draw_group(
                    culling, tile->culling_group.value(), tile->scene_views & views_bitset);
                return false;
            }
            else
            {
                return true;
            }
        }
        else
        {
#if 0
            HRZ_LOG_ERROR("Cannot schedule tile: tile not found");
#endif
            return true;
        }
    }

    bool always_schedule_instantly() const override { return false; }

    uint64_t next_displayable_frame() const override { return _next_displayable_frame; }

    void schedule_draw_now(uint64_t channel_id, uint64_t tile_id, uint32_t views_bitset) override
    {
        auto it = _tiles_by_id.find({channel_id, tile_id});
        if (it != _tiles_by_id.end())
        {
            auto tile_handle = it->second;
            Tile* tile = _tiles.get_object(tile_handle);
            if (!tile) return;

            Config* config = _configs.get_object(tile->config);
            if (!config) return;

            if (config->status == Config::Status::Ready)
            {
                _tiles_to_render.push_back(std::make_pair(tile_handle, views_bitset));
            }
        }
    }

    bool tile_is_ready(const Tile* tile) const
    {
        return tile->status == Tile::Status::Renderable || tile->status == Tile::Status::Error;
    }

    void remove_tile(TileH tile_handle)
    {
        _loading_tiles.erase(tile_handle);
        _renderable_tiles.erase(tile_handle);
        _removed_tiles.insert(tile_handle);
    }

    struct MakeElementPrototypeContext
    {
        std::vector<uint32_t> parent_indices;
        std::vector<Config::Element::AnchorIndex> anchor_indices;
        uint32_t next_anchor_index = 0;
        uint32_t next_z_index = 0;
    };

    uint32_t make_element_prototype(
        Config* config,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        MakeElementPrototypeContext* ctx,
        const hrz_proto::SymbolElement& descriptor)
    {
        std::optional<uint32_t> parent_index = std::nullopt;
        if (!ctx->parent_indices.empty())
        {
            parent_index = {ctx->parent_indices.back()};
        }

        uint32_t index = config->elements.size();
        ctx->parent_indices.push_back(index);

        config->elements.push_back({{descriptor.type(), 0}, parent_index, {}, {}, {}});

        bool is_anchor = descriptor.type() == hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT;

        std::optional<Config::Element::AnchorIndex> anchor_index = std::nullopt;
        if (is_anchor)
        {
            ctx->anchor_indices.push_back({index, ctx->next_anchor_index});
            ctx->next_anchor_index += 1;
        }
        if (is_anchor || !ctx->anchor_indices.empty())
        {
            anchor_index = {ctx->anchor_indices.back()};
        }

        assert(_element_systems.find(descriptor.type()) != _element_systems.end());
        auto& element_system = _element_systems.at(descriptor.type());

        std::optional<uint32_t> z_index = std::nullopt;
        if (element_system->is_visual())
        {
            if (anchor_index.has_value())
            {
                z_index = {ctx->next_z_index};
                ctx->next_z_index += 1;
            }
            else
            {
                HRZ_LOG_WARNING(
                    "Element of type {} has no ancestor anchor. It will not be displayed.",
                    hrz_proto::SymbolElementType_Name(descriptor.type()));
            }
        }

        auto prototype = element_system->make_prototype(
            descriptor, config->layer_id, z_index.value_or(0), register_prp,
            [this, config, &register_prp, ctx](const hrz_proto::SymbolElement& child) -> uint32_t
            { return make_element_prototype(config, register_prp, ctx, child); });

        config->elements[index].anchor_index = anchor_index;
        config->elements[index].z_index = z_index;
        config->elements[index].prototype = prototype;

        auto anchor_prototype = element_system->get_anchor_prototype(prototype);
        assert(is_anchor == anchor_prototype.has_value());

        if (is_anchor)
        {
            config->anchors.push_back(anchor_prototype.value());

            assert(!ctx->anchor_indices.empty());
            ctx->anchor_indices.pop_back();
        }

        ctx->parent_indices.pop_back();

        return index;
    }

    void make_element_prototypes(
        Config* config,
        const hrz_proto::SymbolElement& root_element_descriptor,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp)
    {
        MakeElementPrototypeContext ctx{};
        make_element_prototype(config, register_prp, &ctx, root_element_descriptor);
    }

    RenderRequest work_removed_configs_and_tiles(WorkCtx& ctx)
    {
        hrz::RenderRequest render_request;

        for (auto handle : _unregistered_configs)
        {
            auto config = _configs.get_object(handle);
            if (!config) continue;

            for (auto& element : config->elements)
            {
                _element_systems.at(element.prototype.type)->delete_prototype(element.prototype);
            }

            _configs.release(handle);
        }
        _unregistered_configs.clear();

        for (auto tile_handle : _removed_tiles)
        {
            auto tile = _tiles.get_object(tile_handle);
            if (tile == nullptr) continue;

            if (tile->status == Tile::Status::Baking)
            {
                hrz_jobs::cancel_job(ctx.js, tile->baking_ticket);
            }

            tile->baking_data = std::nullopt;
            tile->baked_data = std::nullopt;

            if (!tile->ubo.is_null())
            {
                _unused_resources.push_back(tile->ubo);
            }

            if (!tile->anchor_data_texture.is_null())
            {
                _unused_resources.push_back(tile->anchor_data_texture);
            }

            tile->selection_storage.free_gpu_resources(_unused_resources);

            for (auto& renderable_handle : tile->renderables)
            {
                _element_systems.at(renderable_handle.type)->delete_renderable(renderable_handle);
            }

            if (tile->culling_group)
            {
                symbol_culling::unregister_group(ctx.symbol_culling, tile->culling_group.value());
                tile->culling_group = std::nullopt;
            }

            _tiles.release(tile_handle);

            render_request.request_visual_render();
        }
        _removed_tiles.clear();

        return render_request;
    }

    void work_configs(WorkCtx& ctx)
    {
        HRZ_SCOPED_SAMPLE("vector repr symbol work configs");

        for (auto it = _loading_configs.begin(); it != _loading_configs.end();)
        {
            ConfigH config_handle = *it;
            auto config = _configs.get_object(config_handle);

            bool erase = false;

            assert(config->status == Config::Status::Loading);

            bool all_prototypes_ready = true;
            bool has_error = false;

            for (auto& element : config->elements)
            {
                auto prototype_status = _element_systems.at(element.prototype.type)
                                            ->get_prototype_status(element.prototype);

                if (prototype_status == ElementSystem::PrototypeStatus::Error)
                {
                    all_prototypes_ready = false;
                    has_error = true;
                    break;
                }
                else if (prototype_status != ElementSystem::PrototypeStatus::Ready)
                {
                    all_prototypes_ready = false;
                    break;
                }
            }

            if (has_error)
            {
                config->status = Config::Status::Error;
                erase = true;
            }
            else if (all_prototypes_ready)
            {
                for (auto& element : config->elements)
                {
                    element.baking_params = _element_systems.at(element.prototype.type)
                                                ->get_prototype_baking_params(element.prototype);
                }

                config->status = Config::Status::Ready;
                erase = true;
            }

            if (erase)
            {
                _loading_configs.erase(it++);
            }
            else
            {
                ++it;
            }
        }
    }

    RenderRequest work_tiles(WorkCtx& ctx)
    {
        HRZ_SCOPED_SAMPLE("vector repr symbol work tiles");

        hrz::RenderRequest render_request;

        for (auto it = _loading_tiles.begin(); it != _loading_tiles.end();)
        {
            TileH tile_handle = *it;
            auto tile = _tiles.get_object(tile_handle);
            const auto config = _configs.get_object(tile->config);

            bool erase = false;

            auto send_status_update_message = [&]()
            {
                if (tile->id.has_value())
                {
                    auto it = _channels.find(tile->id->channel_id);
                    if (it != _channels.end())
                    {
                        auto& channel = it->second;
                        channel.send(
                            hrz::vt::repr::messages::TileStatusUpdate{tile->id->tile_id, true});
                    }
                }
            };

            if (tile->status == Tile::Status::AwaitingConfig)
            {
                if (config->status == Config::Status::Ready)
                {
                    assert(tile->baking_data.has_value());

                    for (const auto& element : config->elements)
                    {
                        std::optional<uint32_t> anchor_index = std::nullopt;
                        if (element.anchor_index.has_value())
                        {
                            anchor_index = {element.anchor_index->anchor_index};
                        }

                        tile->baking_data->elements.push_back(
                            {element.prototype.type, element.parent_index, anchor_index,
                             element.z_index, element.baking_params});
                    }

                    tile->baking_ticket = hrz_jobs::add_job_bake_symbols(
                        ctx.js, tile->baking_data.value(),
                        {hrz::monitoring::systems::Symbols, tile->layer_id});
                    tile->baking_data = std::nullopt;
                    tile->status = Tile::Status::Baking;
                }
                else if (config->status == Config::Status::Error)
                {
                    tile->status = Tile::Status::Error;
                    send_status_update_message();
                }
            }
            if (tile->status == Tile::Status::Baking)
            {
                if (hrz_jobs::is_job_finished(ctx.js, tile->baking_ticket))
                {
                    if (hrz_jobs::get_job_status(ctx.js, tile->baking_ticket)
                        == hrz::job_scheduler::JobStatus::Finished_Success)
                    {
                        tile->baked_data = {BakedSymbols{}};
                        hrz_jobs::get_job_response(
                            ctx.js, tile->baking_ticket, tile->baked_data.value());

                        tile->center = tile->baked_data->tile_center;
                        tile->bsphere_center = tile->baked_data->bsphere_center;
                        tile->bsphere_radius = tile->baked_data->bsphere_radius;

                        symbol_culling::GroupInfo culling_group_info{};
                        culling_group_info.culling_bsphere =
                            hrz::BSphere<double>{tile->bsphere_center, tile->bsphere_radius};
                        culling_group_info.group_origin = tile->center;
                        culling_group_info.anchors = config->anchors;
                        culling_group_info.representation_z_index =
                            (uint16_t)(config->z_index & 0xffff);

                        auto tile_coords_str = fmt::to_string(tile->coords);

                        tile->culling_group = symbol_culling::register_group(
                            ctx.symbol_culling, std::move(culling_group_info),
                            tile->baked_data->anchor_spans, tile->baked_data->anchor_culling_data,
                            config->ignore_occlusions,
                            {hrz::monitoring::systems::Symbols, tile->layer_id},
                            {{"tile coords"_ss, tile_coords_str}});

                        tile->status = Tile::Status::Baked;
                    }
                    else
                    {
                        hrz_jobs::cancel_job(ctx.js, tile->baking_ticket);
                        tile->status = Tile::Status::Error;
                        send_status_update_message();
                        erase = true;
                    }
                }
            }

            if (erase)
            {
                _loading_tiles.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        return render_request;
    }

    RenderRequest work(WorkCtx& ctx) override
    {
        HRZ_SCOPED_SAMPLE("vector repr symbol work");

        hrz::RenderRequest render_request;

        _channels.work();

        for (auto& it : _channels)
        {
            auto channel_id = it.first;
            auto& channel = it.second;

            for (auto& message : channel.receive())
            {
                std::visit(
                    [&](auto& message)
                    {
                        using MessageType = std::decay_t<decltype(message)>;
                        if constexpr (std::is_same_v<
                                          MessageType, hrz::vt::repr::messages::RegisterStyle>)
                        {
                            decltype(hrz::vt::repr::messages::StyleRegistrationResult::
                                         registered_properties) registered_properties;

                            auto handle = register_style(
                                message.repr, message.layer_id,
                                [repr_reg = ctx.repr_reg, layer_id = message.layer_id,
                                 &registered_properties = registered_properties](
                                    std::string_view name,
                                    const hrz::vector_data::OwnedAttributeValue& default_value)
                                    -> uint64_t
                                {
                                    uint64_t prp_id = repr_reg->register_property(layer_id, name);
                                    registered_properties.push_back({prp_id, default_value});
                                    return prp_id;
                                },
                                ConfigId{channel_id, message.style_id});

                            channel.send(hrz::vt::repr::messages::StyleRegistrationResult{
                                message.style_id, handle.has_value(),
                                std::move(registered_properties)});
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType,
                                               hrz::vt::repr::messages::UnregisterStyle>)
                        {
                            auto it = _configs_by_id.find(ConfigId{channel_id, message.style_id});
                            if (it != _configs_by_id.end())
                            {
                                unregister_style(
                                    it->second,
                                    [repr_reg = ctx.repr_reg,
                                     layer_id = message.layer_id](uint64_t prp_id)
                                    { repr_reg->unregister_property(layer_id, prp_id); });
                                _configs_by_id.erase(it);
                            }
                            else
                            {
                                HRZ_LOG_WARNING("Cannot unregister style: style not found");
                            }
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType, hrz::vt::repr::messages::AddTile>)
                        {
                            auto it = _configs_by_id.find(ConfigId{channel_id, message.style_id});
                            if (it != _configs_by_id.end())
                            {
                                add_tile(
                                    it->second, message.coords, message.layer_id,
                                    message.object_ref, message.feature_ref, message.feature_ids,
                                    message.geometry, message.style,
                                    TileId{channel_id, message.tile_id});
                            }
                            else
                            {
                                HRZ_LOG_ERROR("Cannot add tile: style not found");
                            }
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType, hrz::vt::repr::messages::RemoveTile>)
                        {
                            auto it = _tiles_by_id.find(TileId{channel_id, message.tile_id});
                            if (it != _tiles_by_id.end())
                            {
                                remove_tile(it->second);
                                _tiles_by_id.erase(it);
                            }
                            else
                            {
                                HRZ_LOG_WARNING("Cannot remove tile: tile not found");
                            }
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType,
                                               hrz::vt::repr::messages::UpdateTileElevation>)
                        {
                            // No-op
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType, hrz::vt::repr::messages::UpdateClipId>)
                        {
                            // No-op
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType,
                                               hrz::vt::repr::messages::UpdateLighting>)
                        {
                            // No-op
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType,
                                               hrz::vt::repr::messages::UpdateSelection>)
                        {
                            auto it = _tiles_by_id.find(TileId{channel_id, message.tile_id});
                            if (it != _tiles_by_id.end())
                            {
                                update_selection(it->second, message.selected_objects);
                            }
                            else
                            {
                                HRZ_LOG_ERROR("Cannot update selection: tile not found");
                            }
                        }
                        else
                        {
                            static_assert(hrz::always_false<MessageType>, "Unhandled case");
                        }
                    },
                    message);
            }
        }

        render_request |= work_removed_configs_and_tiles(ctx);

        for (auto& it : _element_systems)
        {
            it.second->work(ctx);
        }

        work_configs(ctx);
        render_request |= work_tiles(ctx);

        _next_displayable_frame = symbol_culling::get_last_culled_frame(ctx.symbol_culling);

        return render_request;
    }

    RenderRequest work_gpu(WorkGpuCtx& ctx) override
    {
        HRZ_SCOPED_SAMPLE("vector repr symbol work gpu");

        hrz::RenderRequest render_request;

        for (auto resource : _unused_resources)
        {
            ctx.render->rc->dealloc(resource);
        }
        _unused_resources.clear();

        for (auto& it : _element_systems)
        {
            it.second->work_gpu(ctx.render);
        }

        for (auto it = _loading_tiles.begin(); it != _loading_tiles.end();)
        {
            TileH tile_handle = *it;
            auto tile = _tiles.get_object(tile_handle);
            const auto config = _configs.get_object(tile->config);

            bool erase = false;

            auto send_status_update_message = [&]()
            {
                if (tile->id.has_value())
                {
                    auto it = _channels.find(tile->id->channel_id);
                    if (it != _channels.end())
                    {
                        auto& channel = it->second;
                        channel.send(
                            hrz::vt::repr::messages::TileStatusUpdate{tile->id->tile_id, true});
                    }
                }
            };

            if (tile->status == Tile::Status::Baked)
            {
#define CHECK_RESOURCE_UPLOAD(resource, resource_type)                                          \
    if (resource.is_null())                                                                     \
    {                                                                                           \
        HRZ_LOG_ERROR(                                                                          \
            "Could not upload {} of tile {}-{}-{} to the GPU", resource_type, tile->coords.lod, \
            tile->coords.x, tile->coords.y);                                                    \
        tile->status = Tile::Status::Error;                                                     \
        send_status_update_message();                                                           \
        erase = true;                                                                           \
        break;                                                                                  \
    }

                auto& baked_data = tile->baked_data.value();

                auto tile_coords_str = fmt::to_string(tile->coords);

                {
                    TileUniformData ubo;
                    hrz::split_double(tile->center.x, ubo.center_low.x, ubo.center_high.x);
                    hrz::split_double(tile->center.y, ubo.center_low.y, ubo.center_high.y);
                    hrz::split_double(tile->center.z, ubo.center_low.z, ubo.center_high.z);
                    ubo.object_reference = tile->object_ref.to_uvec2();
                    ubo.feature_reference = tile->feature_ref.to_uvec3();

                    my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
                    ub_res.size = sizeof(ubo);
                    ub_res.usage = my::UsageHint::Static;
                    ub_res.data = &ubo;
                    tile->ubo = ctx.render->rc->alloc(
                        &ub_res, hrz::monitoring::systems::Symbols, tile->layer_id,
                        {{"tile coords"_ss, tile_coords_str}});
                }

                auto alloc_data_texture =
                    [&](gsl::span<const std::byte> data_buffer, my::TextureFormat format,
                        unsigned int pixels_per_entry, hrz::MetadataString contents_metadata)
                {
                    assert(!my::is_format_compressed(format));
                    size_t bytes_per_entry =
                        my::format_external_pixel_byte_size(format) * pixels_per_entry;
                    size_t entry_count = data_buffer.size() / bytes_per_entry;
                    lm::uvec2 texture_size = hrz::vt::compute_data_texture_size(entry_count);

                    my::TextureResource tex_res;
                    tex_res.layout.type = my::TextureLayout::Type2D;
                    tex_res.layout.format = format;
                    tex_res.layout.width = texture_size.x * pixels_per_entry;
                    tex_res.layout.height = texture_size.y;
                    tex_res.layout.depth = 1;
                    tex_res.layout.levels = 1;
                    tex_res.generate_mipmaps = false;
                    tex_res.allow_allocation_failure = true;
                    tex_res.data = {&data_buffer, 1};

                    auto texture = ctx.render->rc->alloc(
                        &tex_res, hrz::monitoring::systems::Symbols, tile->layer_id,
                        {{"contents"_ss, contents_metadata}, {"tile coords"_ss, tile_coords_str}});

                    return texture;
                };

                {
                    size_t anchor_count = baked_data.anchor_gpu_data.size();
                    auto anchor_data = baked_data.anchor_gpu_data.get_data();

                    if (anchor_count > 0)
                    {
                        constexpr unsigned int pixels_per_anchor =
                            sizeof(BakedSymbols::AnchorGpu) / sizeof(lm::uvec4);
                        tile->anchor_data_texture = alloc_data_texture(
                            hrz::as_bytes(anchor_data.as_span()), my::TextureFormat::RGBA32UI,
                            pixels_per_anchor, "anchors"_ss);
                        CHECK_RESOURCE_UPLOAD(tile->anchor_data_texture, "anchors");
                    }
                    else
                    {
                        tile->anchor_data_texture = my::ResourceHandle::null();
                    }

                    tile->selection_storage =
                        hrz::selection::SelectionStorageUint32TextureMultiIndex(
                            baked_data.max_feature_index + 1,
                            {hrz::monitoring::systems::Symbols, tile->layer_id},
                            {{"tile coords"_ss, tile_coords_str}});

                    if (tile->has_feature_ids)
                    {
                        for (size_t i = 0; i < anchor_count; ++i)
                        {
                            const auto& anchor = anchor_data.at(i);
                            tile->selection_storage.register_indirection(
                                anchor.feature_id, anchor.feature_index);
                        }
                    }
                }

                std::array<my::ResourceHandle, SCENE_VIEW_COUNT> culling_visibility_textures{};
                if (tile->culling_group)
                {
                    culling_visibility_textures = symbol_culling::initialize_visibility_textures(
                        ctx.symbol_culling, tile->culling_group.value(), ctx.render);
                }

                size_t i = 0;
                for (const auto& element : config->elements)
                {
                    if (!element.z_index.has_value()) continue;

                    auto& baked_instances_opt = baked_data.instances[i];

                    if (!baked_instances_opt.has_value())
                    {
                        ++i;
                        continue;
                    }

                    auto& baked_instances = baked_instances_opt.value();

                    if (baked_instances.type != element.prototype.type)
                    {
                        assert(false && "Invalid instance element type");
                        tile->status = Tile::Status::Error;
                        send_status_update_message();
                        break;
                    }

                    assert(element.anchor_index.has_value());
                    auto anchor_ubo = _anchor_element_system->get_prototype_ubo(
                        config->elements.at(element.anchor_index->element_index).prototype);

                    // Use 16 bits to sort between representations, then use
                    // 8 bits of z-index to sort between elements of the same
                    // symbol representation.
                    // Leave the 8 least-significant bits to the element system
                    // to sort between draw calls of a single element if needed.
                    uint32_t z_index = ((config->z_index << 16)
                                        | (((uint32_t)hrz::clamp_cast<size_t, uint8_t>(i)) << 8))
                        << 8;

                    auto renderable_opt = _element_systems.at(baked_instances.type)
                                              ->make_renderable(
                                                  element.prototype, config->layer_id, tile->coords,
                                                  std::move(baked_instances), tile->bsphere_radius,
                                                  tile->bsphere_center, tile->ubo, anchor_ubo,
                                                  tile->anchor_data_texture,
                                                  tile->selection_storage.get_texture(ctx.render),
                                                  culling_visibility_textures,
                                                  _data_texture_sampler, z_index, ctx.render);

                    if (renderable_opt.has_value())
                    {
                        tile->renderables.push_back(std::move(renderable_opt.value()));
                    }
                    else
                    {
                        HRZ_LOG_ERROR(
                            "Could not generate renderable for element at z-index {} for tile "
                            "{} of layer {}",
                            i, tile->coords.lod, tile->coords.x, tile->coords.y, config->layer_id);
                        tile->status = Tile::Status::Error;
                        send_status_update_message();
                        break;
                    }

                    ++i;
                }

                tile->status = Tile::Status::Renderable;
                send_status_update_message();
                _renderable_tiles.insert(tile_handle);

                tile->baked_data = std::nullopt;
                erase = true;
            }

            if (erase)
            {
                _loading_tiles.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        return render_request;
    }

    void draw(DrawCtx& ctx) override
    {
        HRZ_SCOPED_SAMPLE("vector repr symbol draw");

        for (const auto& entry : _tiles_to_render)
        {
            Tile* tile = _tiles.get_object(entry.first);
            if (!tile) continue;

            Config* config = _configs.get_object(tile->config);
            if (!config) continue;

            tile->selection_storage.work_gpu(ctx.render);

            uint32_t scene_views = tile->scene_views & entry.second;
            bool has_any_selected = tile->selection_storage.has_any_selected();

            if (scene_views == 0) continue;

            for (auto renderable_handle : tile->renderables)
            {
                _element_systems.at(renderable_handle.type)
                    ->draw_renderable(
                        renderable_handle, scene_views, has_any_selected, config->ignore_occlusions,
                        ctx.render);
            }
        }

        _tiles_to_render.clear();
    }

    void update_selection(
        TileH tile_handle,
        const hrz::flat_hash_set<vector_data::FeatureIdHash>& selected_objects)
    {
        Tile* tile = _tiles.get_object(tile_handle);
        if (!tile || tile->status != Tile::Status::Renderable) return;

        tile->selection_storage.update_selection(selected_objects);
    }

    std::pair<uint64_t, Channel> create_channel() override { return _channels.create_channel(); }
};
} // namespace
} // namespace symbol

std::unique_ptr<ReprSystem> create_symbol_repr_system()
{
    return std::unique_ptr<ReprSystem>(new symbol::SymbolReprSystem());
}

void collect_symbol_shaders(hrz::GpuResourceContext* rc)
{
    symbol::SymbolReprSystem::collect_shaders(rc);
}
} // namespace hrz::vt
