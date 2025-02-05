#include "planet/hrz_core_planet_surface.h"

#include "hrz_core_channel_group.h"
#include "hrz_core_global_flags.h"
#include "hrz_core_picking_id_allocator.h"
#include "planet/hrz_core_planet_elevation_query.h"
#include "planet/hrz_core_planet_geometry.h"
#include "planet/hrz_core_planet_raster_collection.h"

#include <hrz_common_crs_database.h>
#include <hrz_common_geo.h>
#include <hrz_common_monitoring_defs.h>
#include <hrz_common_profiling.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_inlined_vector.h>
#include <hrz_fnd_maths.h>
#include <hrz_fnd_meta.h>
#include <hrz_fnd_thread.h>
#include <hrz_fnd_time.h>
#include <hrz_protocol_path_builder.h>

#include <array>
#include <limits>
#include <mutex>
#include <queue>

namespace
{
enum
{
    TextureUnitBase = 4,
    SetCenterDelayMs = 1000,
    ClipmapBakeDelayMs = 100,
};

static_assert(
    hrz_proto::RasterGroup_MAX < hrz::MAX_IMAGERY_GROUP_COUNT,
    "Too many imagery raster groups in enum");

static_assert(
    hrz::MAX_IMAGERY_GROUP_COUNT >= hrz::SCENE_VIEW_COUNT,
    "Not enough imagery raster groups for every scene view");

struct PlanetParamsUbo
{
    uint32_t picking_combined_id;
    uint32_t picking_object_id;
    float mipmap_bias;
    uint32_t compensate_inclination;
    uint32_t mix_lods;
    uint32_t _padding[3];
    lm::vec4 clip_center[HRZ_S_CLIPMAP_LOD_COUNT];
};

HRZ_CHECK_UBO_SIZE(PlanetParamsUbo);

float max_screen_space_error_to_mipmap_bias(float max_screen_space_error)
{
    if (max_screen_space_error > 0)
    {
        // A max screen-space error of 1 means that a texel from the raster atlas
        // can be at most 1 pixel wide when displayed on screen. A value of 2 allows
        // the texel to be at most 2 pixels wide. A value of 4, 4 pixels and so on.
        // Each doubling of the max-screen space error corresponds to a whole level
        // of mipmap bias.
        // With no bias, when the mipmap level that is computed for a given texel
        // is anything from 1 to 1.999..., the value gets truncated to 1. It can
        // reach 2 so a bias of 0 is a max-screen space error of 2.
        // Combining these constraints gives the following equation:
        return std::log2(max_screen_space_error) - 1;
    }
    else
    {
        // Failsafe:
        // 0 as max screen-space error signifies that raster must be refined infinitely,
        // so it isn't an acceptable value. When this value is encountered, it's probably
        // because it hasn't been set in the model. In this case, no bias is applied.
        return 0;
    }
}

float mipmap_bias_to_max_screen_space_error(float mipmap_bias)
{
    return std::exp2(mipmap_bias + 1);
}
} // namespace

namespace hrz
{
struct RasterDataFetch
{
    struct Fetch
    {
        hrz::flat_hash_set<planet::RasterDataFetchMergeGroupTicket> img_tickets;
        hrz::flat_hash_set<planet::RasterDataFetchMergeGroupTicket> dtm_tickets;

        planet::RasterDataFetchResult result;
    };

    using FetchPool = GenObjectPool<Fetch, GenIndexPool<planet::RasterDataFetchTicket, 15, 16>, 16>;

    FetchPool fetches;
    hrz::flat_hash_set<planet::RasterDataFetchTicket> working;
    hrz::flat_hash_set<planet::RasterDataFetchTicket> ready;
};

struct TerrainVersionSubscription
{
    uint64_t channel_id;
    uint64_t subscription_id;

    bool operator==(const TerrainVersionSubscription& other) const
    {
        return other.channel_id == channel_id && other.subscription_id == subscription_id;
    }

    template<typename H>
    friend H AbslHashValue(H h, const TerrainVersionSubscription& update)
    {
        return H::combine(std::move(h), update.subscription_id, update.subscription_id);
    }
};

struct PlanetSurface
{
    bool disabled;

    bool compress_atlas_textures;
    PlatformInfo platform_info;
    my::Instance::Info my_instance_info;

    vtex::ClipmapParams clipmap_params;

    int32_t clip_id;
    uint8_t picking_system_id;
    PlanetParamsUbo planet_params_ubo;
    my::ResourceHandle planet_params_ubo_buffer;
    bool must_update_uniforms;

    uint32_t imagery_merge_group_count;
    planet::RasterCollection<planet::DtmRasterCollectionTraits> dtm_rasters;
    planet::RasterCollection<planet::ImageryRasterCollectionTraits> img_rasters;

    my::ResourceHandle nearest_sampler;
    my::ResourceHandle linear_sampler;
    my::ResourceHandle empty_indirection_texture;
    my::ResourceHandle empty_atlas;

    double last_set_center;
    lm::dvec3 last_culler_eye_point;
    double last_clipmap_bake;

    size_t atlas_tile_count;
    float min_mipmap_bias;
    std::array<size_t, 5> request_tile_count_history;
    size_t request_tile_count_history_size;
    bool has_increased_mipmap_bias;

    std::unique_ptr<planet::ElevationQuery> elevation_query;

    hrz::InlinedUniqueVector<AttributionHandle, 8> merged_attributions;

    RasterDataFetch raster_data_fetch;

    bool inclination_compensation_updated;
    bool lod_mixing_updated;
    bool max_screen_space_error_updated;

    uint64_t last_terrain_version = 0;
    hrz::flat_hash_set<TerrainVersionSubscription> terrain_version_subscriptions;

    ChannelGroup<planet::FromPlanetSurfaceMessage, planet::ToPlanetSurfaceMessage> channels;

    PlanetSurface(
        uint32_t imagery_merge_group_count,
        uint32_t atlas_size,
        bool compress_atlas_textures,
        uint32_t default_tile_cache_size,
        const PlatformInfo& platform_info,
        const my::Instance::Info& my_instance_info,
        PickingIdAllocator* pia) :
        disabled(!get_flag(Flag::EnableTerrain)),
        compress_atlas_textures(compress_atlas_textures),
        platform_info(platform_info),
        my_instance_info(my_instance_info),
        clipmap_params(vtex::ClipmapParams(CLIPMAP_SIZE, MERCATOR_TILE_SIZE, CLIPMAP_LOD_COUNT)),
        clip_id(-1),
        must_update_uniforms(true),
        imagery_merge_group_count(hrz::clamp(
            imagery_merge_group_count,
            (uint32_t)1,
            (uint32_t)hrz::MAX_IMAGERY_GROUP_COUNT)),
        min_mipmap_bias(0.0f),
        request_tile_count_history_size(0),
        has_increased_mipmap_bias(false),
        elevation_query(new planet::ElevationQuery()),
        inclination_compensation_updated(false),
        lod_mixing_updated(false),
        max_screen_space_error_updated(false)
    {
        // When disabled, leave DTM rasters working as usual for elevation queries.
        dtm_rasters.init(false, atlas_size, default_tile_cache_size);
        img_rasters.init(disabled, atlas_size, default_tile_cache_size);
        atlas_tile_count = dtm_rasters.atlas_tile_count();

        last_set_center = std::numeric_limits<double>::lowest();
        last_culler_eye_point = {0, 0, 0};
        last_clipmap_bake = std::numeric_limits<double>::lowest();

        picking_system_id = picking::allocate_system_id(pia);
        planet_params_ubo.picking_combined_id = picking::combine_picking_ids(picking_system_id, 0);
        planet_params_ubo.picking_object_id = 0;
        planet_params_ubo.mipmap_bias = max_screen_space_error_to_mipmap_bias(2.0f);
        planet_params_ubo.compensate_inclination = (uint32_t) true;
    }

    RenderRequest update(Render* render)
    {
        RenderRequest render_request;

        const my::Renderer::Culler& culler = render->rd->as_culler();

        double now = now_frame_ms();

        if (now - last_set_center >= SetCenterDelayMs)
        {
            lm::dvec3 eye = culler.get_eye_point();

            if (eye != last_culler_eye_point)
            {
                GeoPosition2 geo = hrz::ecef_to_geo2(eye);
                lm::dvec2 webm = hrz::geo_to_web_mercator_pixels(geo);

                clipmap_params.set_center(lm::uvec2(webm) / hrz::MERCATOR_TILE_SIZE);
                clipmap_params.write_offsets(planet_params_ubo.clip_center);

                must_update_uniforms = true;

                dtm_rasters.recenter(clipmap_params);
                img_rasters.recenter(clipmap_params);
                last_set_center = now;
                last_culler_eye_point = eye;

                render_request.request_visual_render();
                render_request.schedule_planet_feedback();
            }
        }

        if (now - last_clipmap_bake >= ClipmapBakeDelayMs)
        {
            if (dtm_rasters.upload_composed_tiles_and_bake_clipmap())
            {
                render_request.request_visual_render();
                render_request.schedule_planet_feedback();
                render_request.schedule_flat_overlay_render();
            }
            if (img_rasters.upload_composed_tiles_and_bake_clipmap())
            {
                render_request.request_visual_render();
            }
            last_clipmap_bake = now;
        }

        if (must_update_uniforms)
        {
            update_uniforms(render);
            must_update_uniforms = false;
            render_request.request_visual_render();
        }

        return render_request;
    }

    void destroy(
        AssetsLoader* al,
        BlobAllocator* ba,
        JobScheduler* js,
        PickingIdAllocator* pia,
        Render* render)
    {
        dtm_rasters.destroy(al, ba, js, render);
        img_rasters.destroy(al, ba, js, render);

        render->rc->dealloc(planet_params_ubo_buffer);
        render->rc->dealloc(empty_indirection_texture);
        render->rc->dealloc(empty_atlas);
        render->rc->dealloc(nearest_sampler);
        render->rc->dealloc(linear_sampler);

        picking::release_system_id(pia, picking_system_id);
    }

    void initialize_rendering(Render* render)
    {
        {
            my::TextureResource res;
            res.layout.type = my::TextureLayout::Array;
            res.layout.format = my::TextureFormat::RGBA8UI;
            res.layout.width = 1;
            res.layout.height = 1;
            res.layout.depth = 1;
            res.layout.levels = 1;
            res.generate_mipmaps = false;
            res.data = {};

            empty_indirection_texture = render->rc->alloc(&res, monitoring::systems::PlanetSurface);
        }

        {
            my::TextureResource res;
            res.layout.type = my::TextureLayout::Type2D;
            res.layout.format = my::TextureFormat::RGBA8;
            res.layout.width = 1;
            res.layout.height = 1;
            res.layout.depth = 1;
            res.layout.levels = 1;
            res.generate_mipmaps = false;
            res.data = {};

            empty_atlas = render->rc->alloc(&res, monitoring::systems::PlanetSurface);
        }

        {
            my::SamplerResource res;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
            res.use_mipmaps = false;

            nearest_sampler = render->rc->alloc(&res, monitoring::systems::PlanetSurface);

            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;

            linear_sampler = render->rc->alloc(&res, monitoring::systems::PlanetSurface);
        }

        for (unsigned int i = 0; i < imagery_merge_group_count; i++)
        {
            img_rasters.add_group(
                render, clipmap_params, std::string("imagery raster group ") + std::to_string(i),
                compress_atlas_textures, platform_info, my_instance_info);
        }

        dtm_rasters.add_group(
            render, clipmap_params, "DTM raster group", compress_atlas_textures, platform_info,
            my_instance_info);

        {
            my::BufferResource res(my::BufferResource::Uniform);
            res.size = sizeof(PlanetParamsUbo);
            res.usage = my::UsageHint::Updatable;
            res.data = nullptr;
            planet_params_ubo_buffer = render->rc->alloc(&res, monitoring::systems::PlanetSurface);
        }
    }

    void update_uniforms(Render* render)
    {
        if (disabled) return;

        render->my->update_buffer(
            planet_params_ubo_buffer, 0, sizeof(PlanetParamsUbo), &planet_params_ubo);
    }

    bool work_rasters(SceneModel* model, AssetsLoader* al, BlobAllocator* ba, JobScheduler* js)
    {
        bool rasters_have_changed = false;

        rasters_have_changed |= dtm_rasters.work_rasters(model, al, ba, js);
        rasters_have_changed |= img_rasters.work_rasters(model, al, ba, js);

        return rasters_have_changed;
    }

    void work(
        AssetsLoader* al,
        BlobAllocator* ba,
        JobScheduler* js,
        SceneModel* model,
        AttributionRegistry* attributions,
        gsl::span<const RenderViewInfo> views_info,
        gsl::span<PlanetGeometry*> geometries)
    {
        HRZ_SCOPED_SAMPLE("planet work");

        channels.work();

        for (auto& it : channels)
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
                                          MessageType,
                                          planet::surface::messages::
                                              SubscribeToTerrainVersionUpdates>)
                        {
                            TerrainVersionSubscription subscription_id{
                                channel_id, message.subscription_id};
                            if (!terrain_version_subscriptions.contains(subscription_id))
                            {
                                terrain_version_subscriptions.insert(subscription_id);
                            }
                            else
                            {
                                HRZ_LOG_ERROR(
                                    "Duplicated terrain version update subscription ID: {}-{}",
                                    subscription_id.channel_id, subscription_id.subscription_id);
                            }
                        }
                        else if constexpr (std::is_same_v<
                                               MessageType,
                                               planet::surface::messages::
                                                   CancelTerrainVersionUpdatesSubscription>)
                        {
                            terrain_version_subscriptions.erase(
                                {channel_id, message.subscription_id});
                        }
                        else if constexpr (
                            std::is_same_v<
                                MessageType, planet::surface::messages::RequestTileElevationBounds>)
                        {
                            double min_elevation = 0.0;
                            double max_elevation = 0.0;
                            bool success = dtm_rasters.get_tile_bounds(
                                message.coords, &min_elevation, &max_elevation);

                            channel.send(planet::surface::messages::TileElevationBounds{
                                message.request_id,
                                success ? std::optional<double>{min_elevation} : std::nullopt,
                                success ? std::optional<double>{max_elevation} : std::nullopt});
                        }
                        else
                        {
                            static_assert(hrz::always_false<MessageType>, "Unhandled case");
                        }
                    },
                    message);
            }
        }

        {
            uint64_t new_terrain_version = dtm_rasters.get_bounds_tracker_version();
            if (new_terrain_version != last_terrain_version)
            {
                last_terrain_version = new_terrain_version;

                for (auto& subscription : terrain_version_subscriptions)
                {
                    auto it = channels.find(subscription.channel_id);
                    if (it != channels.end())
                    {
                        auto& channel = it->second;
                        channel.send(planet::surface::messages::TerrainVersionUpdate{
                            subscription.subscription_id, new_terrain_version});
                    }
                }
            }
        }

        for (auto* geometry : geometries)
        {
            ::hrz::planet::work(geometry, ba, js, model, &clipmap_params);
        }

        std::vector<gsl::span<const TileCoordsWithUsage>> requested_tile_lists;
        size_t requested_tiles_hash = 0;

        bool requested_tiles_updated = false;
        for (auto* geometry : geometries)
        {
            auto tiles = planet::get_updated_requested_tiles(geometry);
            requested_tile_lists.push_back(tiles.tiles);
            requested_tiles_hash = hrz::hash_mix(requested_tiles_hash, tiles.hash);
            requested_tiles_updated |= tiles.was_updated;
        }

        if (requested_tiles_updated)
        {
            size_t requested_tile_count = 0;
            for (const auto& tiles : requested_tile_lists)
            {
                // We should technically first deduplicate the tiles between the geometries,
                // and then count them.
                // But assuming that the multiple views share tiles most of the time, this
                // is faster and should work in most situations.
                if (tiles.size() > requested_tile_count)
                {
                    requested_tile_count = tiles.size();
                }
            }

            assert(request_tile_count_history_size < request_tile_count_history.size());
            request_tile_count_history[request_tile_count_history_size] = requested_tile_count;
            request_tile_count_history_size += 1;

            if (request_tile_count_history_size == request_tile_count_history.size())
            {
                // Average the requested tile count over the last few feedback renders.
                size_t total_count = 0;
                for (size_t count : request_tile_count_history)
                {
                    total_count += count;
                }
                float average_count = (float)total_count / request_tile_count_history_size;

                // We can tolerate requesting a bit too many tiles, but if there isn't enough
                // requested tiles to fill the atlases, we want to readjust the mipmap bias
                // as soon as possible.
                if (average_count > atlas_tile_count * 1.25f || average_count < atlas_tile_count)
                {
                    float factor = average_count / atlas_tile_count;
                    float mipmap_bias_change = std::log2(factor);

                    // Limit the rate of change.
                    mipmap_bias_change = hrz::clamp(std::abs(mipmap_bias_change), 0.0f, 0.5f)
                        * std::copysign(1.0f, mipmap_bias_change);

                    auto new_mipmap_bias = planet_params_ubo.mipmap_bias + mipmap_bias_change;
                    if (new_mipmap_bias < min_mipmap_bias)
                    {
                        new_mipmap_bias = min_mipmap_bias;
                    }

                    if (new_mipmap_bias > planet_params_ubo.mipmap_bias
                        && !has_increased_mipmap_bias)
                    {
                        HRZ_LOG_INFO(
                            "Max screen-space error too low for raster atlas size; adjusting the "
                            "value automatically");
                        has_increased_mipmap_bias = true;
                    }

                    planet_params_ubo.mipmap_bias = new_mipmap_bias;
                    must_update_uniforms = true;
                }

                request_tile_count_history_size = 0;
            }

            dtm_rasters.update_requested_tiles(requested_tile_lists, requested_tiles_hash, al, js);
            img_rasters.update_requested_tiles(requested_tile_lists, requested_tiles_hash, al, js);
        }

        dtm_rasters.work(al, ba, js, attributions, views_info);
        img_rasters.work(al, ba, js, attributions, views_info);

        // Imagery first, it's probably more important
        merged_attributions.clear();
        img_rasters.get_attributions(&merged_attributions);
        dtm_rasters.get_attributions(&merged_attributions);

        elevation_query->work(js, al, ba, &dtm_rasters, dtm_rasters.get_group(0));

        update_raster_data_fetch_progress();

        auto make_raster_path_builder = [&]()
        {
            SceneModelAccessor accessor(model);
            hrz_proto::SceneSettingsPathBuilder<SceneModelAccessor> builder(accessor);
            return builder.raster();
        };

        if (inclination_compensation_updated)
        {
            planet_params_ubo.compensate_inclination =
                (uint32_t)make_raster_path_builder().compensate_inclination().get();
            must_update_uniforms = true;
            inclination_compensation_updated = false;
        }

        if (lod_mixing_updated)
        {
            planet_params_ubo.mix_lods = (uint32_t)make_raster_path_builder().mix_lods().get();
            must_update_uniforms = true;
            lod_mixing_updated = false;
        }

        if (max_screen_space_error_updated)
        {
            planet_params_ubo.mipmap_bias = max_screen_space_error_to_mipmap_bias(
                make_raster_path_builder().max_screen_space_error().get());
            min_mipmap_bias = planet_params_ubo.mipmap_bias;
            request_tile_count_history_size = 0;
            has_increased_mipmap_bias = false;
            must_update_uniforms = true;
            max_screen_space_error_updated = false;
        }
    }

    bool is_working(gsl::span<const PlanetGeometry*> geometries) const
    {
        for (const auto* geometry : geometries)
        {
            if (::hrz::planet::is_working(geometry)) return true;
        }
        return dtm_rasters.is_working() || img_rasters.is_working();
    }

    void fill_in_geometry_resources(
        planet::GeometryResources* resources,
        const my::Instance::Info& my_info)
    {
        resources->planet_params =
            my::UboBinding{0, planet_params_ubo_buffer, 0, sizeof(PlanetParamsUbo)};

        resources->dtm_indirection = my::TextureBinding{
            0, dtm_rasters.get_group(0)->get_clipmap_texture(),
            dtm_rasters.get_group(0)->get_clipmap_sampler()};

        resources->dtm_atlas = my::TextureBinding{
            0, dtm_rasters.get_group(0)->get_table_texture(),
            my_info.has_texture_float_linear ? linear_sampler : nearest_sampler};

        // If there are more rasters than MAX_IMAGERY_GROUP_COUNT, those above
        // the limit are not displayed.
        for (unsigned int i = 0; i < MAX_IMAGERY_GROUP_COUNT; i++)
        {
            if (i < img_rasters.group_count())
            {
                auto* group = img_rasters.get_group(i);

                resources->imagery_indirection[i] = my::TextureBinding{
                    0, group->get_clipmap_texture(), group->get_clipmap_sampler()};

                resources->imagery_atlas[i] =
                    my::TextureBinding{0, group->get_table_texture(), linear_sampler};
            }
            else
            {
                resources->imagery_indirection[i] =
                    my::TextureBinding{0, empty_indirection_texture, nearest_sampler};

                resources->imagery_atlas[i] = my::TextureBinding{0, empty_atlas, linear_sampler};
            }
        }
    }

    RenderRequest work_gpu(Render* render, BlobAllocator* ba)
    {
        RenderRequest render_request = update(render);
        dtm_rasters.work_gpu(render, ba);
        img_rasters.work_gpu(render, ba);

        return render_request;
    }

    void update_raster_data_fetch_progress()
    {
        if (raster_data_fetch.working.empty()) return;

        for (auto it = raster_data_fetch.working.begin(); it != raster_data_fetch.working.end();)
        {
            auto ticket = *it;
            auto* fetch = raster_data_fetch.fetches.get_object(ticket);

            for (auto dtm_it = fetch->dtm_tickets.begin(); dtm_it != fetch->dtm_tickets.end();)
            {
                auto dtm_ticket = *dtm_it;

                auto result = dtm_rasters.try_get_data_fetch_result(dtm_ticket);
                if (result.has_value())
                {
                    fetch->result.results.push_back(result.value());
                    fetch->dtm_tickets.erase(dtm_it++);
                }
                else
                {
                    ++dtm_it;
                }
            }

            for (auto img_it = fetch->img_tickets.begin(); img_it != fetch->img_tickets.end();)
            {
                auto img_ticket = *img_it;

                auto result = img_rasters.try_get_data_fetch_result(img_ticket);
                if (result.has_value())
                {
                    fetch->result.results.push_back(result.value());
                    fetch->img_tickets.erase(img_it++);
                }
                else
                {
                    ++img_it;
                }
            }

            // The fetch is completed once both DTM and imagery fetches have been completed.
            if (fetch->dtm_tickets.empty() && fetch->img_tickets.empty())
            {
                raster_data_fetch.working.erase(it++);
                raster_data_fetch.ready.insert(ticket);
            }
            else
            {
                ++it;
            }
        }
    }
};

namespace planet
{
PlanetSurface* create_surface(
    uint32_t imagery_merge_group_count,
    uint32_t atlas_size,
    bool compress_atlas_textures,
    uint32_t default_tile_cache_size,
    const PlatformInfo& platform_info,
    const my::Instance::Info& my_instance_info,
    PickingIdAllocator* pia)
{
    assert(pia);
    return new PlanetSurface(
        imagery_merge_group_count, atlas_size, compress_atlas_textures, default_tile_cache_size,
        platform_info, my_instance_info, pia);
}

void destroy(
    PlanetSurface* planet,
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js,
    PickingIdAllocator* pia,
    Render* render)
{
    assert(planet);
    planet->destroy(al, ba, js, pia, render);
    delete planet;
}

void initialize_rendering(PlanetSurface* planet, Render* render)
{
    assert(planet);
    return planet->initialize_rendering(render);
}

RenderRequest work_gpu(PlanetSurface* planet, Render* render, BlobAllocator* ba)
{
    assert(planet);
    return planet->work_gpu(render, ba);
}

void use_attributions(PlanetSurface* surface, AttributionRegistry* attributions)
{
    attribution::use_this_frame(attributions, surface->merged_attributions.as_span());
}

void fill_in_geometry_resources(
    PlanetSurface* planet,
    GeometryResources* res,
    const my::Instance::Info& my_info)
{
    planet->fill_in_geometry_resources(res, my_info);
}

uint32_t get_imagery_raster_groups_bitset(
    PlanetSurface* planet,
    hrz_proto::SceneViewIndex scene_view)
{
    assert(planet);
    return planet->img_rasters.get_merge_groups_bitset(scene_view);
}

void work(
    PlanetSurface* planet,
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js,
    SceneModel* model,
    AttributionRegistry* attributions,
    gsl::span<const RenderViewInfo> views_info,
    gsl::span<PlanetGeometry*> geometries)
{
    assert(planet && al && ba && js);
    planet->work(al, ba, js, model, attributions, views_info, geometries);
}

bool is_working(const PlanetSurface* planet, gsl::span<const PlanetGeometry*> geometries)
{
    assert(planet);
    return planet->is_working(geometries);
}

void register_layer(
    PlanetSurface* planet,
    SceneModel* model,
    uint64_t layer_id,
    hrz_proto::LayerType layer_type)
{
    assert(planet && model);

    if (layer_type == hrz_proto::LayerType::DTM_RASTER)
    {
        planet->dtm_rasters.register_layer(model, layer_id);
    }
    else if (layer_type == hrz_proto::LayerType::IMAGERY_RASTER)
    {
        planet->img_rasters.register_layer(model, layer_id);
    }
    else
    {
        HRZ_LOG_ERROR("Unhandled layer type: {}", hrz_proto::LayerType_Name(layer_type));
        assert(false);
    }
}

void unregister_layer(PlanetSurface* planet, uint64_t layer_id, hrz_proto::LayerType layer_type)
{
    assert(planet);

    if (layer_type == hrz_proto::LayerType::DTM_RASTER)
    {
        planet->dtm_rasters.unregister_layer(layer_id);
    }
    else if (layer_type == hrz_proto::LayerType::IMAGERY_RASTER)
    {
        planet->img_rasters.unregister_layer(layer_id);
    }
    else
    {
        HRZ_LOG_ERROR("Unhandled layer type: {}", hrz_proto::LayerType_Name(layer_type));
        assert(false);
    }
}

void notify_model_update(
    PlanetSurface* planet,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::DtmRasterLayerPath& path)
{
    assert(planet);
    assert(path.valid());

    planet->dtm_rasters.notify_model_update(layer_id, update_type, path);
}

void notify_model_update(
    PlanetSurface* planet,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::ImageryRasterLayerPath& path)
{
    assert(planet);
    assert(path.valid());

    planet->img_rasters.notify_model_update(layer_id, update_type, path);
}

void notify_model_update(
    PlanetSurface* planet,
    scene_model::UpdateType update_type,
    const scene_model::SceneSettingsPath& path)
{
    assert(planet);
    assert(path.valid());

    planet->dtm_rasters.notify_model_update(update_type, path);
    planet->img_rasters.notify_model_update(update_type, path);

    if (path.is_raster())
    {
        auto raster_path = path.clone().raster();

        if (raster_path.is_compensate_inclination() || raster_path.leaf())
        {
            planet->inclination_compensation_updated = true;
        }

        if (raster_path.is_mix_lods() || raster_path.leaf())
        {
            planet->lod_mixing_updated = true;
        }

        if (raster_path.is_max_screen_space_error() || raster_path.leaf())
        {
            planet->max_screen_space_error_updated = true;
        }
    }
    else if (path.leaf())
    {
        planet->inclination_compensation_updated = true;
        planet->lod_mixing_updated = true;
        planet->max_screen_space_error_updated = true;
    }
}

void pick(
    PlanetSurface* planet,
    const picking::ObjectReference& obj,
    const lm::dvec3& position,
    gsl::span<const hrz_proto::LayerHandle> included_rasters,
    hrz_proto::SceneViewIndex scene_view,
    hrz_proto::PickResults& pick_results)
{
    assert(planet);

    if (planet->disabled) return;

    // We always want raster data even when clicking on something that is not
    // the terrain. But not when clicking on outer space...
    if (obj.system_id == 0) return;

    planet->dtm_rasters.pick(position, included_rasters, scene_view, pick_results);
    planet->img_rasters.pick(position, included_rasters, scene_view, pick_results);
}

RasterDataFetchTicket schedule_raster_data_fetch(
    PlanetSurface* planet,
    const GeoPosition2& position,
    gsl::span<const hrz_proto::LayerHandle> layers)
{
    assert(planet);

    if (planet->disabled) return {};

    auto ticket = planet->raster_data_fetch.fetches.alloc();
    auto* fetch = planet->raster_data_fetch.fetches.get_object(ticket);

    auto dtm_tickets = planet->dtm_rasters.schedule_raster_data_fetch(position, layers);
    for (auto ticket : dtm_tickets)
    {
        fetch->dtm_tickets.insert(ticket);
    }

    auto img_tickets = planet->img_rasters.schedule_raster_data_fetch(position, layers);
    for (auto ticket : img_tickets)
    {
        fetch->img_tickets.insert(ticket);
    }

    planet->raster_data_fetch.working.insert(ticket);
    return ticket;
}

std::optional<RasterDataFetchResult> retrieve_raster_data_fetch_results(
    PlanetSurface* planet,
    RasterDataFetchTicket ticket)
{
    assert(planet);

    if (planet->disabled) return std::make_optional<RasterDataFetchResult>({});

    std::optional<RasterDataFetchResult> result = std::nullopt;

    if (planet->raster_data_fetch.ready.contains(ticket))
    {
        auto* fetch = planet->raster_data_fetch.fetches.get_object(ticket);
        if (fetch)
        {
            result = fetch->result;
        }

        planet->raster_data_fetch.fetches.release(ticket);
        planet->raster_data_fetch.ready.erase(ticket);
    }

    return result;
}

std::pair<size_t, size_t> make_typed_object_references(
    PlanetSurface* planet,
    gsl::span<const picking::ObjectReference> objs,
    gsl::span<hrz_proto::TypedObjectReference> output)
{
    assert(objs.size() <= output.size());

    if (planet->disabled) return std::make_pair(0, 0);

    if (objs.size() == 0) return std::make_pair(0, 0);

    size_t in_cursor = 0;
    size_t out_cursor = 0;

    // There is no complementary or object id for the planet (for now) so we
    // only need to check the first object reference, and then skip all other
    // with the same system id since they are sorted.
    if (objs[0].system_id == planet->picking_system_id)
    {
        *output[out_cursor++].mutable_planet() = hrz_proto::Void();
        in_cursor++;

        while (in_cursor < objs.size() && objs[in_cursor].system_id == planet->picking_system_id)
        {
            in_cursor++;
        }
    }

    return std::make_pair(in_cursor, out_cursor);
}

bool layer_work(
    PlanetSurface* planet,
    SceneModel* model,
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js)
{
    assert(planet && model && al && js);

    return planet->work_rasters(model, al, ba, js);
}

void raster_group_dev_ui(PlanetSurface* planet, mu_Context* ctx, const char* window_name)
{
    if (mu_begin_window_ex(ctx, window_name, mu_rect(300, 100, 400, 300), MU_OPT_CLOSED))
    {
        fmt::memory_buffer buffer;

        {
            static int layout[] = {130, 70, -1};
            mu_layout_row(ctx, 3, layout, 0);

            mu_text(ctx, "");
            mu_text(ctx, "Configured");
            mu_text(ctx, "Current");

            mu_text(ctx, "Max screen-space error");
            buffer.clear();
            fmt::format_to(
                std::back_inserter(buffer), "{:.2f}",
                mipmap_bias_to_max_screen_space_error(planet->min_mipmap_bias));
            buffer.push_back(0);
            mu_text(ctx, buffer.data());
            buffer.clear();
            fmt::format_to(
                std::back_inserter(buffer), "{:.2f}",
                mipmap_bias_to_max_screen_space_error(planet->planet_params_ubo.mipmap_bias));
            buffer.push_back(0);
            mu_text(ctx, buffer.data());

            mu_text(ctx, "Mipmap bias");
            buffer.clear();
            fmt::format_to(std::back_inserter(buffer), "{:.2f}", planet->min_mipmap_bias);
            buffer.push_back(0);
            mu_text(ctx, buffer.data());
            buffer.clear();
            fmt::format_to(
                std::back_inserter(buffer), "{:.2f}", planet->planet_params_ubo.mipmap_bias);
            buffer.push_back(0);
            mu_text(ctx, buffer.data());
        }

        planet->dtm_rasters.dev_ui(ctx);
        planet->img_rasters.dev_ui(ctx);

        mu_end_window(ctx);
    }
}

ElevationQueryTicket query_elevation(
    PlanetSurface* planet,
    const lm::dvec2& point,
    monitoring::ResourceOwner resource_owner)
{
    assert(planet);
    return planet->elevation_query->start_query(point, resource_owner);
}

ElevationQueryTicket query_elevation(
    PlanetSurface* planet,
    hrz::BlobArrayView<lm::dvec2> points,
    monitoring::ResourceOwner resource_owner)
{
    assert(planet);
    return planet->elevation_query->start_query(std::move(points), resource_owner);
}

void cancel_elevation_query(PlanetSurface* planet, ElevationQueryTicket ticket)
{
    assert(planet);
    planet->elevation_query->queue_cancel(ticket);
}

bool is_elevation_query_ready(const PlanetSurface* planet, ElevationQueryTicket ticket)
{
    assert(planet);
    return planet->elevation_query->is_ready(ticket);
}

uint64_t get_terrain_version(const PlanetSurface* planet)
{
    assert(planet);
    return planet->dtm_rasters.get_bounds_tracker_version();
}

bool get_tile_elevation_bounds(
    const PlanetSurface* planet,
    const hrz::TileCoords& coords,
    double* min_elevation,
    double* max_elevation)
{
    assert(planet);
    return planet->dtm_rasters.get_tile_bounds(coords, min_elevation, max_elevation);
}

std::optional<hrz::BlobArray<float>> retrieve_elevation_query(
    PlanetSurface* planet,
    ElevationQueryTicket ticket)
{
    assert(planet);
    return planet->elevation_query->retrieve(ticket);
}

ElevationQuery* get_elevation_query(PlanetSurface* planet)
{
    assert(planet);
    return planet->elevation_query.get();
}

void elevation_query_dev_ui(PlanetSurface* planet, mu_Context* ctx, const char* window_name)
{
    assert(planet);
    planet->elevation_query->dev_ui(ctx, window_name);
}

SurfaceChannel create_surface_channel(PlanetSurface* planet)
{
    assert(planet);

    return planet->channels.create_channel().second;
}
} // namespace planet
} // namespace hrz
