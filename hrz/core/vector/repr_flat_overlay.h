// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_array.h"
#include "hrz/common/fmt.h" // IWYU pragma: keep
#include "hrz/common/geo.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/vector_tiles/data_texture.h"
#include "hrz/core/channel_group.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/jobs/vector_tiles_jobs_params.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/defs.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/render/resources.h"
#include "hrz/core/selection_storage.h"
#include "hrz/core/vector/flat_overlay.h"
#include "hrz/core/vector/image_loader.h"
#include "hrz/core/vector/repr.h"
#include "hrz/fnd/function_ref.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/meta.h"

#include <lin_maths.h>
#include <mycelium/renderer.h>

#include <array>
#include <optional>
#include <vector>

namespace hrz::vt::flat_overlay
{

struct CommonTileUniformData
{
    lm::vec4 center_low;
    lm::vec4 center_high;
    lm::uvec3 feature_reference;
    hrz::bool32 has_feature_ids;
    lm::uvec2 object_reference;
    uint32_t _padding[2];
};

HRZ_CHECK_UBO_SIZE(CommonTileUniformData);

struct DrawReport
{
    size_t features_drawn;
    size_t animated_features_drawn;

    void reset()
    {
        features_drawn = 0;
        animated_features_drawn = 0;
    }
};

struct BaseRenderable : public my::Renderer::Renderable
{
    my::Renderer::BinMask bin_mask{};
    my::Renderer::ViewMask main_views{};
    lm::dvec3 clamped_center;
    double clamped_radius{};
    lm::dvec3 sea_center;
    double sea_radius{};
    uint32_t z_index{};

    struct BaseRenderData
    {
        DrawReport* draw_report;
        uint32_t scene_views = 0;
        bool has_selected_features = false;
    };

    virtual BaseRenderData& get_base_render_data() = 0;
    virtual void free_resources(std::vector<my::ResourceHandle>& to_free) = 0;
};

struct BaseConfig
{
    enum class Status
    {
        Loading,
        Ready,
        Error,
    };

    Status status;
    uint64_t layer_id;
    uint32_t repr_id;

    uint32_t scene_views = 0;
};

struct TileId
{
    uint64_t channel_id;
    uint64_t tile_id;

    constexpr bool operator ==(const TileId& other) const = default;

    template<typename H>
    friend H AbslHashValue(H h, const TileId& request)
    {
        return H::combine(std::move(h), request.channel_id, request.tile_id);
    }
};

struct BaseTileGeometry
{
    lm::dvec3 sea_center;
    lm::dvec3 clamped_center;
    double sea_radius{};
    double clamped_radius{};
    uint32_t max_feature_index{};

    hrz::BlobArray<hrz::vector_data::FeatureIdHash> feature_ids;
};

template<typename TileGeometryType, typename BakingData>
struct BaseTile
{
    enum class Status
    {
        WaitingForConfig,
        ReadyToBake,
        Baking,
        FinishedBaking,
        Ready,
        Error,
    };

    TileId id;
    uint64_t layer_id;
    hrz::TileCoords coords;
    Status status;

    bool has_feature_ids;

    picking::ObjectReference object_ref;
    picking::FeatureReference feature_ref;

    double min_elevation = 0.0;
    double max_elevation = 0.0;
    std::optional<lm::dbbox2> wmerc_bounds = std::nullopt;

    std::optional<BakingData> bake_data;
    std::optional<TileGeometryType> geometry;

    my::ResourceHandle feature_id_texture;
    hrz::selection::SelectionStorageUint32TextureMultiIndex selection_storage;
    my::ResourceHandle selection_texture;

    uint32_t z_index;
    uint32_t scene_views;
};

template<typename T>
concept FlatOverlayRepresentationTraits =
    requires {
        typename T::Config;
        typename T::TileGeometry;
        typename T::Tile;
        typename T::BakingData;
        typename T::BakedData;
    } && std::derived_from<typename T::Config, BaseConfig>
    && std::derived_from<typename T::TileGeometry, BaseTileGeometry>
    && std::derived_from<typename T::BakingData, hrz_jobs::BaseFlatVectorBakingData>
    && std::derived_from<typename T::BakedData, hrz_jobs::BaseFlatVectorBakedGeometry>
    && std::
        derived_from<typename T::Tile, BaseTile<typename T::TileGeometry, typename T::BakingData>>;

template<FlatOverlayRepresentationTraits Traits>
class FlatOverlayReprSystem : public hrz::vt::ReprSystem
{
    using Config = typename Traits::Config;
    using Tile = typename Traits::Tile;
    using TileGeometry = typename Traits::TileGeometry;
    using BakingData = typename Traits::BakingData;
    using BakedData = typename Traits::BakedData;
    using BaseTile = BaseTile<TileGeometry, BakingData>;

    using ConfigH = uint64_t;
    using TileH = uint64_t;

    using ConfigIndexPool = hrz::GenIndexPool<ConfigH, 32, 32>;
    using ConfigPool = hrz::GenObjectPool<Config, ConfigIndexPool, 64>;

    using TileIndexPool = hrz::GenIndexPool<TileH, 32, 32>;
    using TilePool = hrz::GenObjectPool<Tile, TileIndexPool, 64>;

    ConfigPool _configs;

    struct ConfigId
    {
        uint64_t channel_id;
        uint64_t config_id;

        constexpr bool operator ==(const ConfigId& other) const = default;

        template<typename H>
        friend H AbslHashValue(H h, const ConfigId& request)
        {
            return H::combine(std::move(h), request.channel_id, request.config_id);
        }
    };

    hrz::flat_hash_map<ConfigId, ConfigH> _configs_by_id;
    hrz::flat_hash_set<ConfigH> _loading_configs;

    TilePool _tiles;

    hrz::flat_hash_map<TileId, TileH> _tiles_by_id;

    DrawReport _draw_report;
    size_t _frames_since_last_animation_draw = 0;

    hrz::flat_hash_set<std::pair<TileH, uint32_t>> _drawn_tiles;

    hrz::flat_hash_map<TileH, ConfigH> _waiting_for_config;
    hrz::flat_hash_set<TileH> _to_bake;
    hrz::flat_hash_set<TileH> _baking;
    hrz::flat_hash_set<TileH> _finished_baking;
    hrz::flat_hash_set<TileH> _removed;

    hrz::ChannelGroup<hrz::vt::FromReprMessage, hrz::vt::ToReprMessage> _channels;

protected:
    my::ResourceHandle _empty_feature_id_texture;
    hrz::selection::SelectionStorageUint32TextureMultiIndex _empty_selection_storage;
    my::ResourceHandle _metadata_sampler;

    std::vector<my::ResourceHandle> _resources_to_free;

public:
    ~FlatOverlayReprSystem() override = default;

    void deinit(WorkCtx& ctx, hrz::Render*) override { work_removed_tiles(ctx); }

    void init_render(hrz::Render* render) override
    {
        assert(render);

        {
            uint32_t data[] = {0, 0};
            std::span<const std::byte> data_span = {(const std::byte*)&data, sizeof(data)};

            my::TextureResource texture;
            texture.layout.type = my::TextureLayout::Type2D;
            texture.layout.format = my::TextureFormat::RG32UI;
            texture.layout.width = 1;
            texture.layout.height = 1;
            texture.layout.depth = 1;
            texture.layout.levels = 1;
            texture.data = {&data_span, 1};
            texture.generate_mipmaps = false;

            _empty_feature_id_texture = render->rc->alloc(
                &texture, hrz::monitoring::systems::FlatOverlays, 0,
                {{"contents"_ss, "feature ID empty texture"_ss}});
        }

        _empty_selection_storage = hrz::selection::SelectionStorageUint32TextureMultiIndex(
            0, {hrz::monitoring::systems::FlatOverlays, 0}, {});

        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.mipmap_filter = my::SamplerParams::Filter::Nearest;

            _metadata_sampler = render->rc->alloc(&res, hrz::monitoring::systems::FlatOverlays);
        }
    }

    void deinit_render(hrz::Render* render) override
    {
        assert(render);

        _empty_selection_storage.free_gpu_resources(_resources_to_free);
        for (const my::ResourceHandle res : _resources_to_free)
        {
            render->rc->dealloc(res);
        }
        _resources_to_free.clear();

        render->rc->dealloc(_empty_feature_id_texture);
        render->rc->dealloc(_metadata_sampler);
    }

    bool uses_z_coordinates() const override { return false; }

    void set_and_send_tile_status(BaseTile* tile, Tile::Status status)
    {
        tile->status = status;

        auto it = _channels.find(tile->id.channel_id);
        if (it != _channels.end())
        {
            auto& channel = it->second;
            channel.send(hrz::vt::repr::messages::TileStatusUpdate{tile->id.tile_id, true});
        }
    }

    virtual bool initialize_config(
        const hrz_proto::VectorRepr& repr,
        Config* config,
        const hrz::function_ref<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
        >& register_prp) = 0;

    std::optional<ConfigH> register_style(
        const hrz_proto::VectorRepr& repr,
        uint64_t layer_id,
        const hrz::function_ref<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
        >& register_prp,
        ConfigId style_id)
    {
        if (_configs_by_id.contains(style_id))
        {
            HRZ_LOG_ERROR(
                "Cannot register style: ID {}-{} already in use", style_id.channel_id,
                style_id.config_id);
            return std::nullopt;
        }

        Config config;
        config.layer_id = layer_id;
        config.repr_id = repr.id();
        config.scene_views = repr.scene_views().bits();
        config.status = Config::Status::Loading;

        if (!initialize_config(repr, &config, register_prp))
        {
            HRZ_LOG_ERROR(
                "Cannot register style: failed to initialize config for ID {}-{}",
                style_id.channel_id, style_id.config_id);
            return std::nullopt;
        }

        auto config_handle = _configs.alloc(std::move(config));
        _configs_by_id.insert({style_id, config_handle});
        _loading_configs.insert(config_handle);

        return config_handle;
    }

    virtual void unregister_config(
        const Config* config,
        const hrz::function_ref<void(uint64_t prp_id)>& unregister_property) = 0;

    void unregister_style(
        ConfigH config_handle,
        const hrz::function_ref<void(uint64_t prp_id)>& unregister_property)
    {
        if (!_configs.is_valid(config_handle)) return;

        const Config* cfg = _configs.get_object(config_handle);
        unregister_config(cfg, unregister_property);

        _loading_configs.erase(config_handle);
        _configs.release(config_handle);
    }

    virtual void initialize_tile_with_config(const Config& config, Tile* tile) = 0;

    std::optional<TileH> add_tile(
        ConfigH config_handle,
        hrz::TileCoords coords,
        uint64_t layer_id,
        const hrz::picking::ObjectReference& object_ref,
        const hrz::picking::FeatureReference& feature_ref,
        const hrz::vector_data::FeatureIds& feature_ids,
        const hrz::vt::ReprGeometry& geometry,
        const hrz::style::StyledFeatures& style,
        double min_elevation,
        double max_elevation,
        TileId tile_id)
    {
        if (_tiles_by_id.contains(tile_id))
        {
            HRZ_LOG_ERROR(
                "Cannot add tile: ID {}-{} already in use", tile_id.channel_id, tile_id.tile_id);
            return std::nullopt;
        }

        Config* cfg = _configs.get_object(config_handle);
        if (!cfg)
        {
            HRZ_LOG_ERROR("Unknown config.");
            return std::nullopt;
        }

        Tile tile;
        BaseTile& base_tile = tile;

        base_tile.id = tile_id;
        base_tile.layer_id = layer_id;
        base_tile.coords = coords;
        base_tile.has_feature_ids = feature_ids.has_any_attribute();
        base_tile.feature_ref = feature_ref;
        base_tile.object_ref = object_ref;

        base_tile.min_elevation = min_elevation;
        base_tile.max_elevation = max_elevation;
        base_tile.wmerc_bounds = std::nullopt;
        base_tile.scene_views = cfg->scene_views;
        base_tile.z_index = cfg->z_index;

        BakingData bake_data{};
        bake_data.coords = coords;
        bake_data.repr_id = cfg->repr_id;
        bake_data.feature_ids = base_tile.has_feature_ids
            ? feature_ids.hashes()
            : hrz::BlobArray<hrz::vector_data::FeatureIdHash>();
        bake_data.geometry = geometry.geometry;

        auto& dst_style = bake_data.style;
        dst_style.prps = style.prps;
        dst_style.values = style.values;
        dst_style.out_of_line_data = style.out_of_line_data;
        dst_style.instances = style.instances;

        base_tile.bake_data = {std::move(bake_data)};

        TileH handle = _tiles.alloc(std::move(tile));

        {
            auto tile = _tiles.get_object(handle);

            if (cfg->status == Config::Status::Ready)
            {
                prepare_tile_for_baking(tile, cfg);
                _to_bake.insert(handle);
            }
            else if (cfg->status == Config::Status::Error)
            {
                set_and_send_tile_status(tile, Tile::Status::Error);
            }
            else
            {
                assert(cfg->status == Config::Status::Loading);

                tile->status = Tile::Status::WaitingForConfig;
                _waiting_for_config.insert({handle, config_handle});
            }
        }

        _tiles_by_id.insert({tile_id, handle});

        return handle;
    }

    void prepare_tile_for_baking(Tile* tile, const Config* cfg)
    {
        assert(tile->bake_data.has_value());
        assert(cfg->status == Config::Status::Ready);

        initialize_tile_with_config(*cfg, tile);

        tile->status = Tile::Status::ReadyToBake;
    }

    void schedule_draw_now(uint64_t channel_id, uint64_t tile_id, uint32_t views_bitset) override
    {
        auto it = _tiles_by_id.find({channel_id, tile_id});
        if (it != _tiles_by_id.end())
        {
            _drawn_tiles.insert(std::make_pair(it->second, views_bitset));
        }
    }

    void remove_tile(TileH handle) { _removed.insert(handle); }

    void remove_from_set(TileH handle, Tile::Status status)
    {
        switch (status)
        {
            case Tile::Status::WaitingForConfig: _waiting_for_config.erase(handle); break;
            case Tile::Status::ReadyToBake: _to_bake.erase(handle); break;
            case Tile::Status::Baking: _baking.erase(handle); break;
            case Tile::Status::FinishedBaking: _finished_baking.erase(handle); break;
            default: break;
        }
    }

    virtual BaseRenderable* get_renderable(Tile* tile) const = 0;

    void compute_tile_bsphere(Tile* tile) const
    {
        if (!tile->wmerc_bounds.has_value())
        {
            return;
        }

        const auto& wmerc_bounds = tile->wmerc_bounds.value();

        std::array<lm::dvec3, 8> points;

        for (size_t i = 0; i < 4; ++i)
        {
            points[i * 2 + 0] =
                hrz::web_mercator_to_ecef(lm::corner(wmerc_bounds, i), tile->min_elevation);
            points[i * 2 + 1] =
                hrz::web_mercator_to_ecef(lm::corner(wmerc_bounds, i), tile->max_elevation);
        }

        auto bsphere = hrz::compute_bounding_sphere(std::span<const lm::dvec3>(points));

        if (tile->geometry.has_value())
        {
            tile->geometry->clamped_center = bsphere.center;
            tile->geometry->clamped_radius = bsphere.radius;
        }

        if (auto* renderable = get_renderable(tile); renderable != nullptr)
        {
            renderable->clamped_center = bsphere.center;
            renderable->clamped_radius = bsphere.radius;
        }
    }

    void set_tile_elevation(TileH handle, double min_elevation, double max_elevation)
    {
        auto tile = _tiles.get_object(handle);
        if (tile != nullptr)
        {
            tile->min_elevation = min_elevation;
            tile->max_elevation = max_elevation;

            compute_tile_bsphere(tile);
        }
    }

    virtual void work_load_config(WorkCtx& ctx, Config* config) = 0;

    virtual void cancel_baking_job(WorkCtx& ctx, Tile* tile) = 0;

    void work_remove_tile(WorkCtx& ctx, TileH handle, Tile* tile)
    {
        if (tile->status == Tile::Status::Baking)
        {
            cancel_baking_job(ctx, tile);
        }

        if (tile->has_feature_ids)
        {
            _resources_to_free.push_back(tile->feature_id_texture);
            tile->selection_storage.free_gpu_resources(_resources_to_free);
        }

        if (auto* renderable = get_renderable(tile); renderable != nullptr)
        {
            renderable->free_resources(_resources_to_free);
        }

        remove_from_set(handle, tile->status);
        _tiles.release(handle);
    }

    // Returns whether this tile should be erased from the _waiting_for_config set or not.
    bool work_waiting_for_config(WorkCtx& ctx, TileH handle, Tile* tile, Config* config)
    {
        assert(tile->status == Tile::Status::WaitingForConfig);
        assert(tile->bake_data.has_value());

        if (config)
        {
            if (config->status == Config::Status::Ready)
            {
                prepare_tile_for_baking(tile, config);
                work_start_baking(ctx, handle, tile);
                return true;
            }
            else if (config->status == Config::Status::Error)
            {
                set_and_send_tile_status(tile, Tile::Status::Error);
                return true;
            }
            else
            {
                assert(config->status == Config::Status::Loading);
                return false;
            }
        }
        else
        {
            set_and_send_tile_status(tile, Tile::Status::Error);
            return true;
        }
    }

    virtual void start_baking_job(WorkCtx& ctx, Tile* tile) = 0;

    void work_start_baking(WorkCtx& ctx, TileH handle, Tile* tile)
    {
        assert(tile->status == Tile::Status::ReadyToBake);
        assert(tile->bake_data.has_value());

        start_baking_job(ctx, tile);
        tile->bake_data = std::nullopt;
        tile->status = Tile::Status::Baking;

        _baking.insert(handle);
    }

    virtual bool is_baking_job_finished(WorkCtx& ctx, Tile* tile) = 0;
    virtual bool has_baking_job_succeeded(WorkCtx& ctx, Tile* tile) = 0;

    virtual void initialize_tile_geometry_with_baked_data(
        TileGeometry* geometry,
        BakedData& baked_data) = 0;

    virtual void get_baking_job_response(WorkCtx& ctx, Tile* tile, BakedData& out_baked_data) = 0;

    // Returns whether this tile should be erased from the _baking set or not.
    bool work_continue_baking(WorkCtx& ctx, TileH handle, Tile* tile)
    {
        assert(tile->status == Tile::Status::Baking);

        if (is_baking_job_finished(ctx, tile))
        {
            if (has_baking_job_succeeded(ctx, tile))
            {
                BakedData response;
                get_baking_job_response(ctx, tile, response);

                TileGeometry geometry;
                geometry.sea_center = response.sea_bsphere_center;
                geometry.sea_radius = response.sea_bsphere_radius;
                geometry.clamped_center = response.sea_bsphere_center;
                geometry.clamped_radius = response.sea_bsphere_radius;
                geometry.max_feature_index = response.max_feature_index;
                geometry.feature_ids = std::move(response.feature_ids);

                initialize_tile_geometry_with_baked_data(&geometry, response);

                tile->geometry = std::move(geometry);

                tile->wmerc_bounds = {response.wmerc_bounds};
                compute_tile_bsphere(tile);

                tile->status = Tile::Status::FinishedBaking;
                _finished_baking.insert(handle);
            }
            else
            {
                HRZ_LOG_ERROR(
                    "Could not bake flat overlay representation for tile {}-{}-{}",
                    tile->coords.lod, tile->coords.x, tile->coords.y);
                set_and_send_tile_status(tile, Tile::Status::Error);
            }

            return true;
        }
        else
        {
            return false;
        }
    }

    virtual bool tile_geometry_has_necessary_baked_data(const Tile& tile) const = 0;
    virtual bool initialize_renderable(hrz::Render* render, Tile* tile) = 0;

    void work_finished_baking(hrz::Render* render, TileH handle, Tile* tile)
    {
        assert(tile->status == Tile::Status::FinishedBaking);
        assert(tile->geometry.has_value());

#define CHECK_RESOURCE_UPLOAD(resource, resource_type)                                    \
    if (resource.is_null())                                                               \
    {                                                                                     \
        HRZ_LOG_ERROR(                                                                    \
            "Could not upload flat vector {} of tile {}-{}-{} to the GPU", resource_type, \
            tile->coords.lod, tile->coords.x, tile->coords.y);                            \
        set_and_send_tile_status(tile, Tile::Status::Error);                              \
        return;                                                                           \
    }

        if (!tile_geometry_has_necessary_baked_data(*tile))
        {
            // No data to render
            set_and_send_tile_status(tile, Tile::Status::Ready);
            return;
        }

        auto tile_coords_str = fmt::to_string(tile->coords);
        auto& geometry = tile->geometry.value();

        // Selection bitmask texture
        if (tile->has_feature_ids)
        {
            tile->selection_storage = hrz::selection::SelectionStorageUint32TextureMultiIndex(
                geometry.max_feature_index + 1,
                {hrz::monitoring::systems::FlatOverlays, tile->layer_id},
                {{"tile coords"_ss, tile_coords_str}});

            auto feature_ids_data = geometry.feature_ids.get_data();

            for (uint32_t i = 0; i <= geometry.max_feature_index; ++i)
            {
                tile->selection_storage.register_indirection(feature_ids_data.at(i), i);
            }

            tile->selection_texture = tile->selection_storage.get_texture(render);
        }
        else
        {
            tile->selection_texture = _empty_selection_storage.get_texture(render);
        }

        // Feature IDs texture
        if (tile->has_feature_ids)
        {
            auto feature_ids_data = geometry.feature_ids.get_data();
            assert(
                feature_ids_data.size() % hrz::vt::DATA_TEXTURE_SIZE == 0
                || feature_ids_data.size() < hrz::vt::DATA_TEXTURE_SIZE);

            auto data = feature_ids_data.as_bytes();
            auto texture_size = hrz::vt::compute_data_texture_size(feature_ids_data.size());

            my::TextureResource tex_res;
            tex_res.layout.type = my::TextureLayout::Type2D;
            tex_res.layout.format = my::TextureFormat::RG32UI;
            tex_res.layout.width = texture_size.x;
            tex_res.layout.height = texture_size.y;
            tex_res.layout.depth = 1;
            tex_res.layout.levels = 1;
            tex_res.data = {&data, 1};
            tex_res.generate_mipmaps = false;
            tex_res.allow_allocation_failure = true;

            tile->feature_id_texture = render->rc->alloc(
                &tex_res, hrz::monitoring::systems::FlatOverlays, tile->layer_id,
                {{"contents"_ss, "feature IDs"_ss}, {"tile coords"_ss, tile_coords_str}});
            CHECK_RESOURCE_UPLOAD(tile->feature_id_texture, "feature ID texture");
        }
        else
        {
            tile->feature_id_texture = _empty_feature_id_texture;
        }

        if (!initialize_renderable(render, tile))
        {
            set_and_send_tile_status(tile, Tile::Status::Error);
            return;
        }

        {
            BaseRenderable* renderable = get_renderable(tile);

            auto& base_render_data = renderable->get_base_render_data();
            base_render_data.scene_views = tile->scene_views;
            base_render_data.draw_report = &_draw_report;

            renderable->z_index = tile->z_index;
            renderable->bin_mask = hrz::RenderFlatOverlayBin;
            renderable->sea_center = geometry.sea_center;
            renderable->sea_radius = geometry.sea_radius;
            renderable->clamped_center = geometry.clamped_center;
            renderable->clamped_radius = geometry.clamped_radius;
        }

        tile->geometry = std::nullopt;
        set_and_send_tile_status(tile, Tile::Status::Ready);

#undef CHECK_RESOURCE_UPLOAD
    }

    hrz::RenderRequest work_removed_tiles(WorkCtx& ctx)
    {
        hrz::RenderRequest render_request;

        for (TileH handle : _removed)
        {
            Tile* tile = _tiles.get_object(handle);
            if (!tile) continue;

            work_remove_tile(ctx, handle, tile);
            render_request.schedule_flat_overlay_render();
        }
        _removed.clear();

        return render_request;
    }

    hrz::RenderRequest work(WorkCtx& ctx) override
    {
        hrz::RenderRequest render_request;

        _channels.work();

        for (auto& [channel_id, channel] : _channels)
        {
            for (auto& generic_message : channel.receive())
            {
                std::visit(
                    hrz::overload{
                        [&](const hrz::vt::repr::messages::RegisterStyle& message)
                        {
                            decltype(hrz::vt::repr::messages::StyleRegistrationResult::
                                         registered_properties) registered_properties;

                            auto handle = register_style(
                                message.repr, message.layer_id,
                                [repr_reg = ctx.repr_reg, layer_id = message.layer_id,
                                 &registered_properties](
                                    std::string_view name,
                                    const hrz::vector_data::OwnedAttributeValue& default_value)
                                {
                                    uint64_t prp_id = repr_reg->register_property(layer_id, name);
                                    registered_properties.push_back({prp_id, default_value});
                                    return prp_id;
                                },
                                ConfigId{channel_id, message.style_id});

                            channel.send(
                                hrz::vt::repr::messages::StyleRegistrationResult{
                                    message.style_id, handle.has_value(),
                                    std::move(registered_properties)
                                });
                        },
                        [&](const hrz::vt::repr::messages::UnregisterStyle& message)
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
                        },
                        [&](const hrz::vt::repr::messages::AddTile& message)
                        {
                            auto it = _configs_by_id.find(ConfigId{channel_id, message.style_id});
                            if (it != _configs_by_id.end())
                            {
                                add_tile(
                                    it->second, message.coords, message.layer_id,
                                    message.object_ref, message.feature_ref, message.feature_ids,
                                    message.geometry, message.style, message.min_elevation,
                                    message.max_elevation, TileId{channel_id, message.tile_id});
                            }
                            else
                            {
                                HRZ_LOG_ERROR("Cannot add tile: style not found");
                            }
                        },
                        [&](const hrz::vt::repr::messages::RemoveTile& message)
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
                        },
                        [&](const hrz::vt::repr::messages::UpdateTileElevation& message)
                        {
                            auto it = _tiles_by_id.find(TileId{channel_id, message.tile_id});
                            if (it != _tiles_by_id.end())
                            {
                                set_tile_elevation(
                                    it->second, message.min_elevation, message.max_elevation);
                            }
                            else
                            {
                                HRZ_LOG_WARNING("Cannot set tile elevation: tile not found");
                            }
                        },
                        [&](const hrz::vt::repr::messages::UpdateClipId&)
                        {
                            // No-op
                        },
                        [&](const hrz::vt::repr::messages::UpdateLighting&)
                        {
                            // No-op
                        },
                        [&](const hrz::vt::repr::messages::UpdateSelection& message)
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
                    },
                    generic_message);
            }
        }

        for (auto it = _loading_configs.begin(); it != _loading_configs.end();)
        {
            ConfigH handle = *it;
            Config* config = _configs.get_object(handle);
            bool erase = false;

            if (config)
            {
                work_load_config(ctx, config);
                erase = config->status == Config::Status::Ready
                    || config->status == Config::Status::Error;
            }
            else
            {
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

        render_request |= work_removed_tiles(ctx);

        for (auto it = _waiting_for_config.begin(); it != _waiting_for_config.end();)
        {
            TileH tile_handle = it->first;
            ConfigH config_handle = it->second;

            Tile* tile = _tiles.get_object(tile_handle);
            Config* config = _configs.get_object(config_handle);

            bool erase = true;

            if (tile)
            {
                erase = work_waiting_for_config(ctx, tile_handle, tile, config);
            }

            if (erase)
            {
                _waiting_for_config.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        for (TileH handle : _to_bake)
        {
            Tile* tile = _tiles.get_object(handle);
            if (!tile) continue;

            work_start_baking(ctx, handle, tile);
        }
        _to_bake.clear();

        for (auto it = _baking.begin(); it != _baking.end();)
        {
            TileH handle = *it;
            Tile* tile = _tiles.get_object(handle);
            bool erase = true;

            if (tile)
            {
                erase = work_continue_baking(ctx, handle, tile);
            }

            if (erase)
            {
                _baking.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        // Animated features require a constant redrawing of the flat overlays as long as they are
        // visible on screen.
        // Note: We track the amount of consecutive frames drawn without animated features because
        // there is a one frame delay between a flat overlay render request and the actual render.
        // This information is necessary to avoid standby frames between flat overlay renders.
        if (_draw_report.animated_features_drawn > 0)
        {
            _frames_since_last_animation_draw = 0;
        }
        else
        {
            _frames_since_last_animation_draw++;
        }

        if (_frames_since_last_animation_draw < 2)
        {
            render_request.request_visual_render(hrz::RenderRequest::VisualCause::Animation);
            render_request.schedule_flat_overlay_render();
        }

        _draw_report.reset();

        return render_request;
    }

    hrz::RenderRequest work_gpu(WorkGpuCtx& ctx) override
    {
        hrz::RenderRequest render_request;

        _empty_selection_storage.work_gpu(ctx.render);

        for (const TileH handle : _finished_baking)
        {
            Tile* tile = _tiles.get_object(handle);
            if (!tile) continue;

            work_finished_baking(ctx.render, handle, tile);
        }
        _finished_baking.clear();

        for (const my::ResourceHandle res : _resources_to_free)
        {
            ctx.render->rc->dealloc(res);
        }
        _resources_to_free.clear();

        return render_request;
    }

    void draw(DrawCtx& ctx) override
    {
        for (const auto& [tile_handle, scene_views] : _drawn_tiles)
        {
            Tile* tile = _tiles.get_object(tile_handle);
            if (!tile) continue;

            auto* renderable = get_renderable(tile);
            if (!renderable) continue;

            if (tile->has_feature_ids)
            {
                tile->selection_storage.work_gpu(ctx.render);
            }

            renderable->get_base_render_data().scene_views = tile->scene_views & scene_views;
            renderable->main_views = ctx.render->main_views;

            ctx.render->rd->collect_renderable(*renderable);
        }
        _drawn_tiles.clear();
    }

    void update_selection(
        TileH handle,
        const hrz::flat_hash_set<hrz::vector_data::FeatureIdHash>& selected_objects)
    {
        Tile* tile = _tiles.get_object(handle);
        auto* renderable = get_renderable(tile);
        if (tile && tile->status == Tile::Status::Ready && tile->has_feature_ids
            && renderable != nullptr)
        {
            tile->selection_storage.update_selection(selected_objects);
            const bool has_selected_features = tile->selection_storage.has_any_selected();

            renderable->get_base_render_data().has_selected_features = has_selected_features;
        }
    }

    std::pair<uint64_t, Channel> create_channel() override { return _channels.create_channel(); }
};

}; // namespace hrz::vt::flat_overlay
