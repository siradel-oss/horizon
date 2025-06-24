#pragma once

#include "hrz_core_render.h"
#include "hrz_core_scene_model.h"
#include "hrz_core_scene_path.h"
#include "hrz_core_visibility_constraints.h"
#include "planet/hrz_core_planet_raster.h"
#include "planet/hrz_core_planet_raster_data_fetch_types.h"
#include "planet/hrz_core_planet_raster_merge_group.h"
#include "vtex/hrz_core_vtex_clipmap_params.h"

#include <hrz_common_geo.h>
#include <hrz_common_monitoring_defs.h>
#include <hrz_common_proto_maths.h>
#include <hrz_common_tickets.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_static_vector.h>
#include <hrz_protocol_path_builder.h>

#include <array>

namespace hrz::planet
{
namespace details
{
assets_loader::Queue get_priority_queue(
    hrz_proto::LayerType layer_type,
    hrz_proto::RasterGroup group,
    int8_t loading_priority);

bool raster_model_is_complete(const hrz_proto::Raster& model, hrz_proto::LayerType layer_type);

std::unique_ptr<Raster> raster_from_model(
    uint64_t raster_id,
    uint64_t unique_id,
    const hrz_proto::Raster& raster_model,
    uint32_t slot,
    const hrz_proto::RasterGroup& group,
    const hrz_proto::LayerType& layer_type,
    bool is_visible,
    const hrz_proto::LayerVisibilityConstraintList& visibility_constraints,
    uint32_t scene_views,
    uint32_t default_tile_cache_size);

lm::dbbox2 project_to_web_mercator(const hrz_proto::GeographicBounds& wgs84_bounds);

static inline uint32_t compute_slot(hrz_proto::RasterGroup group, uint32_t slot)
{
    return ((uint32_t)group << 24) + (slot & 0xffffff);
}
} // namespace details

// Wish we could use concepts here...

struct ImageryRasterCollectionTraits
{
    template<typename Accessor>
    using PathBuilder = hrz_proto::ImageryRasterLayerPathBuilder<Accessor>;

    using Path = scene_model::ImageryRasterLayerPath;

    using Model = hrz_proto::ImageryRasterLayer;

    static constexpr const char* NAME = "Imagery";
    static constexpr hrz_proto::LayerType LAYER_TYPE = hrz_proto::LayerType::IMAGERY_RASTER;
    static constexpr std::array<hrz_proto::ImageFormat, 1> SOURCE_TILE_IMAGE_FORMATS = {
        hrz_proto::ImageFormat::SRGBA_8};
    static constexpr hrz_proto::ImageFormat COMPOSED_TILE_IMAGE_FORMAT =
        hrz_proto::ImageFormat::SRGBA_8;
    static constexpr planet::TileRequestOrigin TILE_REQUEST_ORIGINS =
        planet::TileRequestOrigin::FeedbackOrigin;
    static constexpr bool TRACK_TILE_BOUNDS = false;

    static double pixel_to_value(const void*) { return 0.0; }

    static my::TextureFormat get_atlas_format(
        bool compress_textures,
        const PlatformInfo& platform_info,
        const my::Instance::Info& my_instance_info)
    {
        if (compress_textures)
        {
            if (platform_info.is_mobile() && my_instance_info.has_etc2_texture_compression)
            {
                return my::TextureFormat::RGBA_ETC2_EAC;
            }
            else if (my_instance_info.has_bc1_bc2_bc3_texture_compression)
            {
                return my::TextureFormat::RGBA_BC3;
            }
        }

        return my::TextureFormat::RGBA8;
    }

    template<typename Accessor>
    static hrz_proto::RasterGroup get_raster_group(const PathBuilder<Accessor>& path_builder)
    {
        return path_builder.clone().group().get();
    }

    template<typename Accessor>
    static uint32_t get_scene_views(const PathBuilder<Accessor>& path_builder)
    {
        return path_builder.clone().scene_views().bits().get();
    }

    static hrz_proto::RasterGroup get_raster_group(const Model& model) { return model.group(); }

    static uint32_t get_scene_views(const Model& model) { return model.scene_views().bits(); }

    inline static bool path_is_group(const Path& path) { return path.is_group(); }

    inline static bool path_is_scene_views(const Path& path) { return path.is_scene_views(); }

    static Model get_default_layer_data()
    {
        Model imagery_data;

        imagery_data.set_visible(true);
        auto* raster = imagery_data.mutable_raster();
        auto* display_bounds = raster->mutable_display_bounds();
        auto* sampling = raster->mutable_sampling();
        auto* blending = raster->mutable_blending();

        imagery_data.mutable_scene_views()->set_bits((1 << SCENE_VIEW_COUNT) - 1);

        display_bounds->set_west(-180);
        display_bounds->set_south(-90);
        display_bounds->set_east(180);
        display_bounds->set_north(90);

        sampling->set_filtering(hrz_proto::TextureFiltering::NEAREST);
        sampling->set_nodata_handling(hrz_proto::NodataHandling::DISCARD_NODATA_PIXELS);
        sampling->set_alpha_channel_usage(hrz_proto::AlphaChannelUsage::IGNORE_ALPHA_CHANNEL);

        blending->set_opacity(1.0f);

        imagery_data.set_group(hrz_proto::RasterGroup::MIDDLE_RASTER_GROUP);
        imagery_data.set_slot(0); // @Todo Get lowest free slot from the model

        return imagery_data;
    }

    static hrz_proto::PathRoot make_path_root(uint64_t layer_id)
    {
        hrz_proto::PathRoot root;
        root.mutable_imagery_raster_layer()->set_opaque(layer_id);
        return root;
    }
};

struct DtmRasterCollectionTraits
{
    template<typename Accessor>
    using PathBuilder = hrz_proto::DtmRasterLayerPathBuilder<Accessor>;

    using Path = scene_model::DtmRasterLayerPath;

    using Model = hrz_proto::DtmRasterLayer;

    static constexpr const char* NAME = "Elevation";
    static constexpr hrz_proto::LayerType LAYER_TYPE = hrz_proto::LayerType::DTM_RASTER;
    static constexpr std::array<hrz_proto::ImageFormat, 5> SOURCE_TILE_IMAGE_FORMATS = {
        hrz_proto::ImageFormat::R_F32, hrz_proto::ImageFormat::R_F32_SILICIUM,
        hrz_proto::ImageFormat::SIGNED_FIXED_24_8, hrz_proto::ImageFormat::TERRARIUM,
        hrz_proto::ImageFormat::TERRAIN_RGB};
    static constexpr hrz_proto::ImageFormat COMPOSED_TILE_IMAGE_FORMAT =
        hrz_proto::ImageFormat::R_F32;
    static constexpr planet::TileRequestOrigin TILE_REQUEST_ORIGINS =
        (TileRequestOrigin)(planet::TileRequestOrigin::FeedbackOrigin
                            | planet::TileRequestOrigin::CameraVerticalProjectionOrigin);
    static constexpr bool TRACK_TILE_BOUNDS = true;

    static double pixel_to_value(const void* pixel)
    {
        float v;
        std::memcpy(&v, pixel, sizeof(float));
        return v;
    }

    static my::TextureFormat get_atlas_format(
        bool compress_textures,
        const PlatformInfo&,
        const my::Instance::Info&)
    {
        return my::TextureFormat::R32F;
    }

    template<typename Accessor>
    static hrz_proto::RasterGroup get_raster_group(const PathBuilder<Accessor>& path_builder)
    {
        return hrz_proto::RasterGroup::BOTTOM_RASTER_GROUP;
    }

    template<typename Accessor>
    static uint32_t get_scene_views(const PathBuilder<Accessor>& path_builder)
    {
        return (1 << SCENE_VIEW_COUNT) - 1;
    }

    static hrz_proto::RasterGroup get_raster_group(const Model& model)
    {
        return hrz_proto::RasterGroup::BOTTOM_RASTER_GROUP;
    }

    static uint32_t get_scene_views(const Model& model) { return (1 << SCENE_VIEW_COUNT) - 1; }

    inline static bool path_is_group(const Path& path) { return false; }

    inline static bool path_is_scene_views(const Path& path) { return false; }

    static Model get_default_layer_data()
    {
        Model dtm_data;
        dtm_data.set_visible(true);

        auto* display_bounds = dtm_data.mutable_raster()->mutable_display_bounds();
        display_bounds->set_west(-180);
        display_bounds->set_south(-90);
        display_bounds->set_east(180);
        display_bounds->set_north(90);

        auto* sampling = dtm_data.mutable_raster()->mutable_sampling();
        sampling->set_filtering(hrz_proto::TextureFiltering::BILINEAR);
        sampling->set_nodata_handling(hrz_proto::NodataHandling::SET_NODATA_TO_ZERO);
        dtm_data.set_slot(0); // @Todo Get lowest free slot from the model

        return dtm_data;
    }

    static hrz_proto::PathRoot make_path_root(uint64_t layer_id)
    {
        hrz_proto::PathRoot root;
        root.mutable_dtm_raster_layer()->set_opaque(layer_id);
        return root;
    }
};

class IRasterCollection
{
public:
    virtual Raster* get_raster_by_id(uint64_t raster_id) = 0;
    virtual Raster* get_raster_by_index(size_t raster_index) = 0;

    virtual const Raster* get_raster_by_id(uint64_t raster_id) const = 0;
};

// A raster collection is populated with all raster providers of a single type
// (imagery or DTM). It is used to distribute the providers between the raster
// groups according to the number of groups available for each scene view.
//
// An instance can be populated with rasters. Each raster has
// its raster provider, which deals with getting the source
// images and configuring the reprojection. The rasters are
// ordered and their composition is configured through
// blending functions.
template<typename Traits>
class RasterCollection : public IRasterCollection
{
    using LayerPathBuilder = typename Traits::template PathBuilder<hrz::SceneModelAccessor>;
    using LayerPath = typename Traits::Path;
    using LayerModel = typename Traits::Model;

    struct LayerUpdate
    {
        uint64_t layer_id;
        scene_model::UpdateType update_type;
        LayerPath path;
    };

    struct SceneViewRasterMergeGroup
    {
        std::unique_ptr<RasterMergeGroup> group;

        // List of scene views the rasters in this merge group will be visible in.
        uint32_t scene_views_bitset;

        // List of raster groups that will be present in this merge group.
        uint32_t raster_groups_bitset;
    };

    bool _disabled = false;

    uint32_t _atlas_size = 0;
    uint32_t _atlas_tile_count = 0;
    uint32_t _default_tile_cache_size = 0;

    std::vector<SceneViewRasterMergeGroup> _groups;
    std::vector<uint64_t> _created_rasters;
    std::vector<uint64_t> _destroyed_rasters;
    std::vector<LayerUpdate> _raster_updates;
    uint32_t _merge_groups_bitset_per_view[SCENE_VIEW_COUNT];

    bool _scene_views_updated = false;
    uint32_t _scene_views = 0;

    TicketGenerator<uint64_t> _unique_id_generator;
    std::vector<std::unique_ptr<Raster>> _rasters;
    hrz::flat_hash_map<uint64_t, unsigned int> _raster_indices_by_id;
    bool _should_sort_rasters = false;

public:
    void init(bool disabled, uint32_t atlas_size, uint32_t default_tile_cache_size)
    {
        _disabled = disabled;
        _atlas_size = std::max(atlas_size, (uint32_t)ATLAS_TILE_SIZE);
        _atlas_tile_count = _atlas_size / ATLAS_TILE_SIZE;
        _default_tile_cache_size = default_tile_cache_size;
    }

    inline size_t atlas_tile_count() const { return _atlas_tile_count * _atlas_tile_count; }

    void add_group(
        hrz::Render* render,
        const vtex::ClipmapParams& clipmap_params,
        const std::string& group_name,
        bool compress_atlas_textures,
        const hrz::PlatformInfo& platform_info,
        const my::Instance::Info& my_instance_info)
    {
        std::unique_ptr<vtex::IndirectionClipmap> clipmap(new vtex::IndirectionClipmap(
            render, clipmap_params, monitoring::systems::PlanetSurface, "raster group",
            group_name));

        const uint32_t page_size = (_disabled) ? 1 : ATLAS_TILE_SIZE;
        const uint32_t page_count = (_disabled) ? 1 : _atlas_tile_count;
        std::unique_ptr<vtex::PageTable> page_table(new vtex::PageTable(
            render, page_size, page_count, page_count,
            Traits::get_atlas_format(compress_atlas_textures, platform_info, my_instance_info),
            monitoring::systems::PlanetSurface, "raster group", group_name));

        std::unique_ptr<vtex::PageCacheManager> page_cache(
            new vtex::PageCacheManager(std::move(page_table), std::move(clipmap)));

        std::optional<TileBoundsTracker> tile_bounds = std::nullopt;
        if (Traits::TRACK_TILE_BOUNDS)
        {
            tile_bounds.emplace();
            tile_bounds.value().pixel_to_value = &Traits::pixel_to_value;
        }

        _groups.push_back(
            {std::make_unique<RasterMergeGroup>(
                 _atlas_size, std::move(page_cache), Traits::SOURCE_TILE_IMAGE_FORMATS,
                 Traits::COMPOSED_TILE_IMAGE_FORMAT, std::move(tile_bounds), group_name),
             0, 0});
    }

    inline size_t group_count() const { return _groups.size(); }

    inline RasterMergeGroup* get_group(size_t index)
    {
        assert(index < _groups.size());
        return _groups[index].group.get();
    }

    void recenter(const vtex::ClipmapParams& clipmap_params)
    {
        for (auto& group : _groups)
        {
            group.group->recenter(clipmap_params);
        }
    }

    bool upload_composed_tiles_and_bake_clipmap()
    {
        bool tiles_uploaded = false;

        for (auto& group : _groups)
        {
            tiles_uploaded |= group.group->upload_composed_tiles_and_bake_clipmap();
        }

        return tiles_uploaded;
    }

    void update_requested_tiles(
        gsl::span<const gsl::span<const RequestedTileCoords>> requested_tiles,
        size_t requested_tiles_hash,
        AssetsLoader* al,
        JobScheduler* js)
    {
        for (auto& group : _groups)
        {
            group.group->update_requested_tiles(
                requested_tiles, requested_tiles_hash, Traits::TILE_REQUEST_ORIGINS, this, al, js);
        }
    }

    void work(
        AssetsLoader* al,
        BlobAllocator* ba,
        JobScheduler* js,
        AttributionRegistry* attributions,
        gsl::span<const RenderViewInfo> views_info)
    {
        uint32_t merge_groups_restart_needed_bitset = 0;

        for (const auto& raster : _rasters)
        {
            if (!raster->is_visible) continue;

            layers::MultiviewVisibilityConstraints visibility_constraints_result =
                layers::are_visibility_constraints_satisfied(
                    views_info, raster->visibility_constraints);

            visibility_constraints_result.satisfied_in &= raster->scene_views;

            if (raster->visibility_constraints_result != visibility_constraints_result)
            {
                uint32_t changed_bitset = (raster->visibility_constraints_result.satisfied_in
                                           ^ visibility_constraints_result.satisfied_in)
                    | (raster->visibility_constraints_result.active_views
                       ^ visibility_constraints_result.active_views);

                changed_bitset &= raster->scene_views;

                for (size_t i = 0; i < _groups.size(); ++i)
                {
                    if ((_groups[i].scene_views_bitset & changed_bitset)
                        && (raster->merge_groups_bitset & (1 << i)))
                    {
                        merge_groups_restart_needed_bitset |= (1 << i);
                    }
                }

                raster->visibility_constraints_result = visibility_constraints_result;
            }
        }

        if (merge_groups_restart_needed_bitset)
        {
            for (size_t i = 0; i < _groups.size(); ++i)
            {
                if (merge_groups_restart_needed_bitset & (1 << i))
                {
                    _groups[i].group->restart_tiles(this, js);
                }
            }
        }

        for (const auto& raster : _rasters)
        {
            if (raster->is_visible && raster->visibility_constraints_result.satisfied_in)
            {
                raster->provider->work(al, ba, js, attributions);
            }
        }

        for (auto& group : _groups)
        {
            group.group->work(this, al, ba, js);
        }
    }

    void get_attributions(hrz::InlinedUniqueVector<AttributionHandle, 8>* attributions) const
    {
        for (auto& group : _groups)
        {
            group.group->get_attributions(attributions);
        }
    }

    void work_gpu(Render* render, BlobAllocator* ba)
    {
        for (auto& group : _groups)
        {
            group.group->work_gpu(render, ba);
        }
    }

    bool is_working() const
    {
        for (const auto& raster : _rasters)
        {
            if (raster->is_visible && raster->visibility_constraints_result.satisfied_in
                && raster->provider->is_working())
                return true;
        }

        for (auto& group : _groups)
        {
            if (group.group->is_working(this)) return true;
        }

        return false;
    }

    void destroy(AssetsLoader* al, BlobAllocator* ba, JobScheduler* js, Render* render)
    {
        for (auto& group : _groups)
        {
            group.group->destroy(al, js, render);
        }

        for (auto& raster : _rasters)
        {
            raster->provider->cancel_jobs_and_release_tiles(al, ba, js);
        }
    }

    void work_views(SceneModel* model)
    {
        hrz::SceneModelAccessor accessor(model);
        hrz_proto::SceneSettingsPathBuilder<hrz::SceneModelAccessor> path(accessor);
        auto scene_settings = path.get();
        _scene_views = scene_settings.active_views().bits();
        _should_sort_rasters = true;
    }

    bool work_rasters(SceneModel* model, AssetsLoader* al, BlobAllocator* ba, JobScheduler* js)
    {
        bool rasters_have_changed = false;

        if (_scene_views_updated)
        {
            work_views(model);
            _scene_views_updated = false;
            rasters_have_changed = true;
        }

        for (auto& raster_id : _created_rasters)
        {
            hrz::SceneModelAccessor accessor(model);
            hrz_proto::LayerHandle handle;
            handle.set_opaque(raster_id);

            LayerPathBuilder builder(accessor, handle);
            auto raster_model = builder.clone().raster().get();

            if (details::raster_model_is_complete(raster_model, Traits::LAYER_TYPE))
            {
                bool is_visible = builder.clone().visible().get();
                auto visibility_constraints = builder.clone().visibility_constraints().get();
                auto group = Traits::get_raster_group(builder);
                auto slot = details::compute_slot(group, builder.clone().slot().get());
                auto scene_views = Traits::get_scene_views(builder);

                auto raster = details::raster_from_model(
                    raster_id, _unique_id_generator.generate(), raster_model, slot, group,
                    Traits::LAYER_TYPE, is_visible, visibility_constraints, scene_views,
                    _default_tile_cache_size);

                add_raster(std::move(raster), al, js);
            }

            rasters_have_changed = true;
        }

        for (uint64_t raster_id : _destroyed_rasters)
        {
            remove_raster(raster_id, al, ba, js);
            rasters_have_changed = true;
        }

        for (auto& update : _raster_updates)
        {
            uint64_t raster_id = update.layer_id;
            auto& path = update.path;

            hrz::SceneModelAccessor accessor(model);
            hrz_proto::LayerHandle handle;
            handle.set_opaque(update.layer_id);

            LayerPathBuilder builder(accessor, handle);

            auto model = builder.get();
            const auto& raster_model = model.raster();

            bool is_visible = model.visible();
            const auto& visibility_constraints = model.visibility_constraints();

            auto group = Traits::get_raster_group(model);
            auto scene_views = Traits::get_scene_views(model);
            auto slot = details::compute_slot(group, model.slot());

            auto create_raster = [&]() -> std::unique_ptr<Raster>
            {
                return details::raster_from_model(
                    raster_id, _unique_id_generator.generate(), raster_model, slot, group,
                    Traits::LAYER_TYPE, is_visible, visibility_constraints, scene_views,
                    _default_tile_cache_size);
            };

            auto reload_raster = [&]()
            {
                remove_raster(raster_id, al, ba, js);
                auto raster = details::raster_from_model(
                    raster_id, _unique_id_generator.generate(), raster_model, slot, group,
                    Traits::LAYER_TYPE, is_visible, visibility_constraints, scene_views,
                    _default_tile_cache_size);
                add_raster(std::move(raster), al, js);
            };

            if (Traits::path_is_group(path))
            {
                auto raster = get_raster_by_id(raster_id);
                if (raster)
                {
                    raster->raster_group = group;
                    set_raster_slot(
                        raster_id, slot, js); // Update slot, as it dependns on the group.
                }
                // If no group contains the raster, it means that it wasn't complete and
                // displayed anyway, so no need to do anything.
            }
            else
            {
                if (path.leaf())
                {
                    remove_raster(raster_id, al, ba, js);
                }

                bool is_raster_currently_displayed = has_raster(raster_id);
                bool model_is_complete =
                    details::raster_model_is_complete(raster_model, Traits::LAYER_TYPE);

                if (is_raster_currently_displayed && !model_is_complete)
                {
                    remove_raster(raster_id, al, ba, js);
                }
                else if (!is_raster_currently_displayed && model_is_complete)
                {
                    auto raster = create_raster();
                    add_raster(std::move(raster), al, js);
                }
                else if (is_raster_currently_displayed && model_is_complete)
                {
                    if (path.is_raster())
                    {
                        auto raster_path = path.clone().raster();

                        if (raster_path.is_provider())
                        {
                            auto provider_path = raster_path.provider();

                            if (provider_path.leaf() || provider_path.is_type())
                            {
                                reload_raster();
                            }
                            else
                            {
                                auto raster = get_raster_by_id(raster_id);
                                auto action = raster->provider->notify_model_update(
                                    provider_path, raster_model.provider());
                                if (action != RasterProvider::UpdateAction::KeepTiles)
                                {
                                    switch (action)
                                    {
                                        case RasterProvider::UpdateAction::RestartTiles:
                                            restart_tiles(raster->merge_groups_bitset, js);
                                            break;
                                        case RasterProvider::UpdateAction::RecreateProvider:
                                            reload_raster();
                                            break;
                                        default: assert(false && "Unhandled case"); break;
                                    }
                                }
                            }
                        }
                        else if (raster_path.is_display_bounds())
                        {
                            auto display_bounds = raster_model.display_bounds();
                            set_raster_display_bounds(raster_id, display_bounds, js);
                        }
                        else if (raster_path.is_blending())
                        {
                            auto blending = raster_model.blending();
                            set_raster_blending(raster_id, blending, js);
                        }
                        else if (raster_path.is_sampling())
                        {
                            auto sampling = raster_model.sampling();
                            set_raster_sampling(raster_id, sampling, js);
                        }
                        else if (raster_path.is_loading_priority())
                        {
                            auto raster = get_raster_by_id(raster_id);
                            raster->loading_priority =
                                clamp_cast<int32_t, int8_t>(raster_model.loading_priority());
                            raster->provider->set_load_queue(details::get_priority_queue(
                                Traits::LAYER_TYPE, group, raster->loading_priority));
                        }
                    }
                    else if (path.is_slot())
                    {
                        set_raster_slot(raster_id, slot, js);
                    }
                    else if (path.is_visible())
                    {
                        set_raster_visibility(raster_id, is_visible, js);
                    }
                    else if (path.is_visibility_constraints())
                    {
                        set_raster_visibility_constraints(raster_id, visibility_constraints);
                    }
                    else if (Traits::path_is_scene_views(path))
                    {
                        set_scene_views(raster_id, scene_views);
                    }
                }
            }

            rasters_have_changed = true;
        }

        _raster_updates.clear();
        _created_rasters.clear();
        _destroyed_rasters.clear();

        if (_should_sort_rasters)
        {
            sort_rasters(js);
            _should_sort_rasters = false;
        }

        return rasters_have_changed;
    }

    void register_layer(SceneModel* model, uint64_t layer_id)
    {
        hrz_proto::PathRoot root = Traits::make_path_root(layer_id);
        scene_model::register_element(model, root);

        // Default data
        const LayerModel default_layer_data = Traits::get_default_layer_data();

        hrz_proto::LayerHandle handle;
        handle.set_opaque(layer_id);

        hrz::SceneModelAccessor accessor(model);
        LayerPathBuilder builder(accessor, handle);
        builder.set(default_layer_data);

        _created_rasters.push_back(layer_id);
    }

    void unregister_layer(uint64_t layer_id) { _destroyed_rasters.push_back(layer_id); }

    void notify_model_update(
        uint64_t layer_id,
        scene_model::UpdateType update_type,
        const LayerPath& path)
    {
        LayerUpdate update{layer_id, update_type, path.clone()};
        _raster_updates.push_back(std::move(update));
    }

    void notify_model_update(
        scene_model::UpdateType update_type,
        const scene_model::SceneSettingsPath& path)
    {
        if (path.leaf() || path.is_active_views() || path.is_main_view())
        {
            _scene_views_updated = true;
        }
    }

    void pick(
        const lm::dvec3& position,
        gsl::span<const hrz_proto::LayerHandle> included_rasters,
        hrz_proto::SceneViewIndex scene_view,
        hrz_proto::PickResults& pick_results)
    {
        uint32_t scene_view_mask = (1 << scene_view);
        for (auto& group : _groups)
        {
            if (group.scene_views_bitset & scene_view_mask)
            {
                group.group->pick(this, position, included_rasters, pick_results);
            }
        }
    }

    std::vector<RasterDataFetchMergeGroupTicket> schedule_raster_data_fetch(
        const GeoPosition2& position,
        gsl::span<const hrz_proto::LayerHandle> layers)
    {
        std::vector<RasterDataFetchMergeGroupTicket> tickets;

        for (auto& group : _groups)
        {
            group.group->schedule_raster_data_fetch(this, position, layers, tickets);
        }

        return tickets;
    }

    std::optional<hrz_proto::PickLayerResult> try_get_data_fetch_result(
        RasterDataFetchMergeGroupTicket ticket)
    {
        for (const auto& group : _groups)
        {
            if (group.group->is_data_fetch_ready(ticket))
            {
                return group.group->get_data_fetch_result(ticket);
            }
        }
        return std::nullopt;
    }

    void dev_ui(mu_Context* ctx)
    {
        for (size_t i = 0; i < _groups.size(); ++i)
        {
            std::string name = fmt::format("{} merge group #{}", Traits::NAME, i);
            _groups[i].group->dev_ui(
                this, ctx, name.c_str(), _groups[i].scene_views_bitset,
                _groups[i].raster_groups_bitset);
        }
    }

    bool has_raster(uint64_t raster_id) const
    {
        return _raster_indices_by_id.find(raster_id) != _raster_indices_by_id.end();
    }

    Raster* get_raster_by_id(uint64_t raster_id) override
    {
        auto it = _raster_indices_by_id.find(raster_id);
        if (it != _raster_indices_by_id.end())
        {
            return _rasters[it->second].get();
        }
        else
        {
            return nullptr;
        }
    }

    const Raster* get_raster_by_id(uint64_t raster_id) const override
    {
        auto it = _raster_indices_by_id.find(raster_id);
        if (it != _raster_indices_by_id.end())
        {
            return _rasters[it->second].get();
        }
        else
        {
            return nullptr;
        }
    }

    Raster* get_raster_by_index(size_t raster_index) override
    {
        if (raster_index < _rasters.size())
        {
            return _rasters[raster_index].get();
        }
        else
        {
            return nullptr;
        }
    }

    uint32_t get_merge_groups_bitset(hrz_proto::SceneViewIndex scene_view) const
    {
        return _merge_groups_bitset_per_view[scene_view];
    }

    uint64_t get_bounds_tracker_version() const
    {
        assert(Traits::TRACK_TILE_BOUNDS);
        uint64_t version = 0;
        for (const auto& group : _groups)
        {
            version += group.group->get_bounds_tracker_version();
        }
        return version;
    }

    bool get_tile_bounds(const hrz::TileCoords& coords, double* min, double* max) const
    {
        assert(Traits::TRACK_TILE_BOUNDS);
        for (const auto& group : _groups)
        {
            if (group.group->get_tile_bounds(coords, min, max))
            {
                return true;
            }
        }
        return false;
    }

    std::pair<double, double> get_bounds_min_max() const
    {
        assert(Traits::TRACK_TILE_BOUNDS);

        std::pair<double, double> min_max = {
            std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()};

        for (const auto& group : _groups)
        {
            auto group_min_max = group.group->get_bounds_min_max();
            min_max.first = std::min(min_max.first, group_min_max.first);
            min_max.second = std::max(min_max.second, group_min_max.second);
        }

        return min_max;
    }

private:
    void sort_rasters(JobScheduler* js)
    {
        static constexpr size_t RASTER_GROUP_COUNT = hrz_proto::TOP_RASTER_GROUP + 1;

        uint32_t scene_views_count = count_set_bits(_scene_views);

        // First we build the list of active views.
        StaticVector<hrz_proto::SceneViewIndex, SCENE_VIEW_COUNT> scene_views;
        for (size_t bit = 0; bit < SCENE_VIEW_COUNT; ++bit)
        {
            if (_scene_views & (1 << bit))
            {
                scene_views.push_back((hrz_proto::SceneViewIndex)(hrz_proto::SCENE_VIEW_0 + bit));
            }
        }
        assert(scene_views.size() == scene_views_count);

        size_t merge_group_count_per_view[SCENE_VIEW_COUNT];
        size_t first_merge_group_per_view[SCENE_VIEW_COUNT];

        if (_groups.size() >= scene_views_count)
        {
            // We have enough merge groups to have one per view, so we distribute merge groups
            // amongst the views.

            std::fill_n(merge_group_count_per_view, SCENE_VIEW_COUNT, 0);

            for (size_t merge_groups_index = 0; merge_groups_index < _groups.size();
                 ++merge_groups_index)
            {
                merge_group_count_per_view[scene_views[merge_groups_index % scene_views_count]] +=
                    1;
            }

            size_t first_merge_group_index = 0;
            for (size_t scene_view_index = 0; scene_view_index < SCENE_VIEW_COUNT;
                 ++scene_view_index)
            {
                first_merge_group_per_view[scene_view_index] = first_merge_group_index;
                first_merge_group_index += merge_group_count_per_view[scene_view_index];
            }
        }
        else
        {
            // Otherwise all views will have the same content.

            std::fill_n(merge_group_count_per_view, SCENE_VIEW_COUNT, _groups.size());
            std::fill_n(first_merge_group_per_view, SCENE_VIEW_COUNT, 0);
        }

        for (auto& group : _groups)
        {
            group.scene_views_bitset = 0;
            group.raster_groups_bitset = 0;
        }

        // For each view, we'll distribute the raster groups amongst the
        // allocated merge groups is such a way that raster groups remain
        // contiguous. That is, there will never be top and bottom in a merge
        // group, and middle in another one.
        for (hrz_proto::SceneViewIndex scene_view_index : scene_views)
        {
            size_t first_merge_group = first_merge_group_per_view[scene_view_index];
            size_t merge_group_count = merge_group_count_per_view[scene_view_index];

            size_t groups_per_merge_group = RASTER_GROUP_COUNT / merge_group_count;
            size_t remaining_groups =
                RASTER_GROUP_COUNT - groups_per_merge_group * merge_group_count;
            size_t first_raster_group = 0;

            for (size_t i = 0; i < merge_group_count; ++i)
            {
                uint32_t raster_groups_count_for_this_merge_group = groups_per_merge_group;
                if (remaining_groups > 0)
                {
                    raster_groups_count_for_this_merge_group += 1;
                    remaining_groups -= 1;
                }

                uint32_t raster_groups_bitset =
                    ((1 << raster_groups_count_for_this_merge_group) - 1) << first_raster_group;
                first_raster_group += raster_groups_count_for_this_merge_group;

                _groups[first_merge_group + i].raster_groups_bitset |= raster_groups_bitset;
                _groups[first_merge_group + i].scene_views_bitset |= (1 << scene_view_index);
            }
        }

        // A stable sort is used so that when a raster is added or removed,
        // the other rasters are just shifted and not randomly reordered.
        // (In the case of slot collisions.)

        std::stable_sort(
            _rasters.begin(), _rasters.end(),
            [](const std::unique_ptr<Raster>& a, const std::unique_ptr<Raster>& b)
            { return a->slot < b->slot; });

        rebuild_raster_indices_by_id_map();

        for (auto& raster : _rasters)
        {
            raster->merge_groups_bitset = 0;
        }

        // Now we construct the list of rasters (by index and id) for each merge group.
        for (size_t merge_group_index = 0; merge_group_index < _groups.size(); ++merge_group_index)
        {
            auto& merge_group = _groups[merge_group_index];
            std::vector<CollectionRasterReference> rasters;

            for (size_t index = 0; index < _rasters.size(); ++index)
            {
                auto* raster = _rasters[index].get();

                if ((merge_group.scene_views_bitset & raster->scene_views)
                    && (merge_group.raster_groups_bitset & (1 << raster->raster_group)))
                {
                    rasters.push_back({index, raster->id, raster->unique_id});
                    raster->merge_groups_bitset |= 1 << merge_group_index;
                }
            }

            merge_group.group->set_rasters(this, js, rasters, merge_group.scene_views_bitset);
        }

        // Construct the range of merge groups used for each scene view
        for (uint32_t& bitset : _merge_groups_bitset_per_view)
        {
            bitset = 0;
        }

        for (size_t merge_group_index = 0; merge_group_index < _groups.size(); ++merge_group_index)
        {
            uint32_t scene_views_bitset = _groups[merge_group_index].scene_views_bitset;
            if (_groups[merge_group_index].group->is_empty()) continue;

            for (size_t i = 0; i < SCENE_VIEW_COUNT; ++i)
            {
                if ((scene_views_bitset & (1 << i)))
                {
                    _merge_groups_bitset_per_view[i] |= 1u << merge_group_index;
                }
            }
        }
    }

    void add_raster(std::unique_ptr<Raster> raster, AssetsLoader* al, JobScheduler* js)
    {
        // Check that the raster is not already present.
        // (Or more generally, that there is no id collision.)
        assert(_raster_indices_by_id.find(raster->id) == _raster_indices_by_id.end());

        for (auto& group : _groups)
        {
            group.group->check_raster_format(raster.get());
        }

        _rasters.push_back(std::move(raster));
        rebuild_raster_indices_by_id_map();

        _should_sort_rasters = true;
    }

    std::unique_ptr<Raster> remove_raster(
        uint64_t raster_id,
        AssetsLoader* al,
        BlobAllocator* ba,
        JobScheduler* js)
    {
        auto it = _raster_indices_by_id.find(raster_id);
        if (it == _raster_indices_by_id.end()) return nullptr;

        auto index = it->second;
        std::unique_ptr<Raster> raster(std::move(_rasters.at(index)));

        {
            auto remove_it = std::next(_rasters.begin(), index);
            _rasters.erase(remove_it);
        }
        rebuild_raster_indices_by_id_map();

        raster->provider->cancel_jobs_and_release_tiles(al, ba, js);
        _should_sort_rasters = true;

        return raster;
    }

    void restart_tiles(uint32_t merge_groups_bitset, JobScheduler* js)
    {
        for (size_t i = 0; i < _groups.size(); ++i)
        {
            if ((merge_groups_bitset & (1 << i)))
            {
                _groups[i].group->restart_tiles(this, js);
            }
        }
    }

    void invalidate_raster_tiles(uint32_t merge_groups_bitset, uint64_t raster_id, JobScheduler* js)
    {
        for (size_t i = 0; i < _groups.size(); ++i)
        {
            if ((merge_groups_bitset & (1 << i)))
            {
                _groups[i].group->invalidate_raster_tiles(this, raster_id, js);
            }
        }
    }

    void set_raster_slot(uint64_t raster_id, uint32_t slot, JobScheduler* js)
    {
        auto index = _raster_indices_by_id.at(raster_id);
        auto& raster = _rasters.at(index);

        raster->slot = slot;

        _should_sort_rasters = true;
        restart_tiles(raster->merge_groups_bitset, js);
    }

    void set_raster_display_bounds(
        uint64_t raster_id,
        const hrz_proto::GeographicBounds& display_bounds,
        JobScheduler* js)
    {
        auto index = _raster_indices_by_id.at(raster_id);
        auto& raster = _rasters.at(index);

        raster->display_bounds = details::project_to_web_mercator(display_bounds);

        invalidate_raster_tiles(raster->merge_groups_bitset, raster->id, js);
    }

    void set_raster_sampling(
        uint64_t raster_id,
        const hrz_proto::RasterSampling& sampling,
        JobScheduler* js)
    {
        auto index = _raster_indices_by_id.at(raster_id);
        auto& raster = _rasters.at(index);

        raster->sampling = sampling;

        restart_tiles(raster->merge_groups_bitset, js);
    }

    void set_raster_blending(
        uint64_t raster_id,
        const HrzProtocol::RasterBlending& blending,
        JobScheduler* js)
    {
        auto index = _raster_indices_by_id.at(raster_id);
        auto& raster = _rasters.at(index);

        raster->blending = blending;

        restart_tiles(raster->merge_groups_bitset, js);
    }

    void set_raster_visibility(uint64_t raster_id, bool is_visible, JobScheduler* js)
    {
        auto index = _raster_indices_by_id.at(raster_id);
        auto& raster = _rasters.at(index);

        raster->is_visible = is_visible;

        restart_tiles(raster->merge_groups_bitset, js);
    }

    void set_raster_visibility_constraints(
        uint64_t raster_id,
        const hrz_proto::LayerVisibilityConstraintList& constraints)
    {
        auto index = _raster_indices_by_id.at(raster_id);
        auto& raster = _rasters.at(index);

        raster->visibility_constraints = constraints;
    }

    void set_scene_views(uint64_t raster_id, uint32_t scene_views)
    {
        auto index = _raster_indices_by_id.at(raster_id);
        auto& raster = _rasters.at(index);

        raster->scene_views = scene_views;
        _should_sort_rasters = true;
    }

    void rebuild_raster_indices_by_id_map()
    {
        _raster_indices_by_id.clear();
        for (unsigned int i = 0; i < _rasters.size(); i++)
        {
            _raster_indices_by_id.insert({_rasters[i]->id, i});
        }
    }
};

} // namespace hrz::planet
