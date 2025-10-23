#include "planet/hrz_core_planet_raster_merge_group.h"

#include "hrz_core_job_scheduler.h"
#include "hrz_core_loading_priorities.h"
#include "planet/hrz_core_planet_raster.h"
#include "planet/hrz_core_planet_raster_collection.h"

#include <hrz_common_blob_allocator.h>
#include <hrz_common_crs_database.h>
#include <hrz_common_fmt.h>
#include <hrz_common_geo.h>
#include <hrz_common_image_processing.h>
#include <hrz_common_planet.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proj.h>
#include <hrz_common_proto_maths.h>
#include <hrz_common_raster_sampling.h>
#include <hrz_common_reprojection.h>
#include <hrz_fnd_format.h>

#include <array>
#include <limits>

namespace hrz::planet
{
namespace
{
// @Todo Invert this so that the value in the config is the actual geometry
// complexity instead of some weird divider.
static const int REPROJ_GRID_SIZE = hrz::ATLAS_TILE_SIZE / 8;

uint32_t compute_num_slots_in_atlas(uint32_t atlas_size)
{
    return (atlas_size / hrz::ATLAS_TILE_SIZE) * (atlas_size / hrz::ATLAS_TILE_SIZE);
}

uint32_t compute_tile_loading_priority(int8_t raster_loading_priority, uint32_t index)
{
    uint16_t max_priority = std::numeric_limits<uint16_t>::max();
    return combine_loading_priorities(
        raster_loading_priority, index >= max_priority ? 0 : max_priority - index);
}

std::optional<hrz_proto::RasterPickResult> get_tile_image_pixel(
    const Raster* raster,
    RasterProvider::SourceLockTicket lock,
    TileCoords coords,
    lm::ivec2 pixel_pos,
    const ImageTilingInfo& info)
{
    hrz_proto::RasterPickResult raster_result;
    bool has_raster_data = false;

    if (lock != RasterProvider::SourceLockTicket::Invalid)
    {
        lm::vec2 uv = pixel_pos / (float)info.provider_tile_pixel_size;

        RasterProvider::TileImage image = raster->provider->get_source_tile_image(lock);
        TileToTileUvTransform<float> uv_xform(coords, image.coords);
        uv = uv_xform(uv);

        pixel_pos = lm::ivec2(uv * (float)info.provider_tile_pixel_size);

        if (!image.image.proto_format().has_value())
        {
            HRZ_LOG_ERROR(
                "Unhandled picking on raster type {}", hrz_proto::LayerType_Name(raster->type));
        }
        else if (
            image.image.valid() && pixel_pos.x >= 0 && pixel_pos.y >= 0
            && pixel_pos.x <= (int32_t)image.image.width()
            && pixel_pos.y <= (int32_t)image.image.height())
        {
            auto image_format = image.image.proto_format().value();

            ImageView input_image(
                image.image.data(), image_format, image.image.width(), image.image.height());

            // Retrieve image pixel value
            // @Todo One day we might need to think about doing
            // interpolation here when needed.

            auto sampling_function = sampling::make_sampling_function(
                hrz_proto::AlphaChannelUsage::USE_ALPHA_CHANNEL,
                raster->provider->get_source_nodata(),
                hrz_proto::NodataHandling::DISCARD_NODATA_PIXELS,
                hrz_proto::TextureFiltering::NEAREST, image_format);

            raster_result.set_image_format(image_format);

            if (image_format == hrz_proto::ImageFormat::SRGBA_8)
            {
                std::array<uint8_t, 4> pixel;
                bool is_nodata = sampling_function->sample(input_image, uv, pixel.data());

                hrz_proto::Color color;
                color.set_r(pixel[0]);
                color.set_g(pixel[1]);
                color.set_b(pixel[2]);
                color.set_a(pixel[3]);
                *raster_result.mutable_color() = color;

                raster_result.set_nodata(is_nodata);

                has_raster_data = true;
            }
            else if (hrz::is_scalar_image_format(image_format))
            {
                std::array<float, 1> pixel;
                bool is_nodata = sampling_function->sample(input_image, uv, pixel.data());

                raster_result.set_number(pixel[0]);

                raster_result.set_nodata(is_nodata);

                has_raster_data = true;
            }
            else
            {
                HRZ_LOG_ERROR(
                    "Unhandled picking on raster type {}", hrz_proto::LayerType_Name(raster->type));
                has_raster_data = false;
            }
        }
    }

    return has_raster_data ? std::optional<hrz_proto::RasterPickResult>{raster_result}
                           : std::nullopt;
}
} // namespace

RasterMergeGroup::RasterMergeGroup(
    uint32_t atlas_size,
    std::unique_ptr<vtex::PageCacheManager> page_cache,
    std::span<const hrz_proto::ImageFormat> source_image_formats,
    hrz_proto::ImageFormat composed_image_format,
    std::optional<TileBoundsTracker>&& bounds_tracker,
    std::string_view name) :
    _atlas_size(atlas_size),
    _num_slots_in_atlas(compute_num_slots_in_atlas(atlas_size)),
    _page_cache(std::move(page_cache)),
    _source_image_formats(source_image_formats),
    _composed_image_format(composed_image_format),
    _name(name.data(), name.size()),
    _bounds_tracker(std::move(bounds_tracker))
{
    _atlas_image_format = _page_cache->get_page_table()->get_format();

    _rasters_hash = compute_rasters_hash({});
    _requested_tile_list_hashes.clear();

    if (_bounds_tracker.has_value())
    {
        _bounds_tracker->reset();
    }
}

void RasterMergeGroup::destroy(AssetsLoader* al, JobScheduler* js, Render* render)
{
    for (auto& tile : _tiles)
    {
        for (auto& tile_raster : tile.second.rasters)
        {
            if (tile_raster.status == ComposedTile::TileRaster::Status::Reprojecting)
            {
                hrz_jobs::cancel_job(js, tile_raster.reproject_tile_ticket);
            }
        }

        if (tile.second.status == ComposedTile::Status::Composing)
        {
            hrz_jobs::cancel_job(js, tile.second.compose_tile_ticket);
        }

        if (tile.second.status == ComposedTile::Status::Compressing)
        {
            hrz_jobs::cancel_job(js, tile.second.compress_tile_ticket);
        }
    }

    _page_cache->destroy(render);
}

void RasterMergeGroup::work_gpu(Render* render, BlobAllocator* ba)
{
    _page_cache->work_gpu(render, ba);
}

bool RasterMergeGroup::check_raster_format(const Raster* raster)
{
    const auto& provider = raster->provider;

    for (auto format : _source_image_formats)
    {
        if (provider->get_image_format() == format)
        {
            return true;
        }
    }

    assert(!"Invalid raster format");
    HRZ_LOG_INFO("Invalid raster format");
    return false;
}

uint64_t RasterMergeGroup::get_bounds_tracker_version() const
{
    assert(_bounds_tracker.has_value());
    return _bounds_tracker->version;
}

bool RasterMergeGroup::get_tile_bounds(const hrz::TileCoords& coords, double* min, double* max)
    const
{
    assert(_bounds_tracker.has_value());
    auto it = _bounds_tracker.value().values.find(coords);
    if (it == _bounds_tracker.value().values.end())
    {
        return false;
    }

    *min = it->second.first;
    *max = it->second.second;
    return true;
}

std::pair<double, double> RasterMergeGroup::get_bounds_min_max() const
{
    assert(_bounds_tracker.has_value());
    return {_bounds_tracker->min_value, _bounds_tracker->max_value};
}

void RasterMergeGroup::cancel_jobs_and_release_tiles(
    IRasterCollection* collection,
    JobScheduler* js,
    ComposedTile::TileRaster& tile_raster,
    bool nominate_tiles_for_eviction)
{
    auto* raster = collection->get_raster_by_id(tile_raster.raster_id);

    // The raster may not be available anymore if it has been deleted. In which
    // case we'll have called "cancel_jobs_and_release_tiles" on the provider
    // so it's fine!
    RasterProvider* provider = nullptr;
    if (raster) provider = raster->provider.get();

    for (auto& reprojected_tile : tile_raster.reprojected_tiles)
    {
        if (provider)
        {
            provider->release_tile(reprojected_tile.lock_ticket, nominate_tiles_for_eviction);
        }

        reprojected_tile.lock_ticket = RasterProvider::LockTicket::Invalid;
    }
    tile_raster.reprojected_tiles.clear();

    if (tile_raster.status == ComposedTile::TileRaster::Status::Reprojecting)
    {
        hrz_jobs::cancel_job(js, tile_raster.reproject_tile_ticket);
    }

    tile_raster.status = ComposedTile::TileRaster::Status::Reprojecting;
}

// Cancel all jobs for the tile and release its source images.
void RasterMergeGroup::cancel_jobs_and_release_tiles(
    IRasterCollection* collection,
    JobScheduler* js,
    ComposedTile& tile,
    bool nominate_tiles_for_eviction)
{
    for (auto& tile_raster : tile.rasters)
    {
        cancel_jobs_and_release_tiles(collection, js, tile_raster, nominate_tiles_for_eviction);
    }

    if (tile.status == ComposedTile::Status::Composing)
    {
        hrz_jobs::cancel_job(js, tile.compose_tile_ticket);
    }
    else if (tile.status == ComposedTile::Status::Compressing)
    {
        hrz_jobs::cancel_job(js, tile.compress_tile_ticket);
    }
    else if (tile.status == ComposedTile::Status::Composed)
    {
        tile.image = {};
    }

    if (_bounds_tracker.has_value())
    {
        _bounds_tracker->values.erase(tile.coords);
    }

    tile.status = ComposedTile::Status::WaitingForProvider;
}

hrz_jobs::ReprojectRasterTileTicket RasterMergeGroup::create_reprojection_job(
    TileCoords tile_coords,
    const Raster* raster,
    JobScheduler* js)
{
    // Only points on the grid are actually projected from the source spatial
    // reference system and the one the planet uses. Between those points the
    // texel positions are interpolated linearly, creating some distortion.
    // The distortion is bigger at the lowest LODs. To counter this, grid points
    // are packed closer to each other for those LODs. The tile at level 0 has
    // the most texels accurately projected.
    unsigned int quad_size = std::min(4 << tile_coords.lod, REPROJ_GRID_SIZE);

    RasterTileReprojParams tile_reproj_params;
    tile_reproj_params.tile_coords = tile_coords;
    tile_reproj_params.quad_size = quad_size;
    tile_reproj_params.raster_geometry = raster->provider->get_geometry();
    tile_reproj_params.raster_display_bounds = raster->display_bounds;

    return hrz_jobs::add_job_reproject_raster_tile(
        js, tile_reproj_params, {monitoring::systems::PlanetSurface, raster->id});
}

void RasterMergeGroup::get_attributions(
    hrz::InlinedUniqueVector<AttributionHandle, 8>* attributions) const
{
    for (unsigned int tile_index = 0;
         tile_index < _sorted_tiles.size() && tile_index < _num_slots_in_atlas; tile_index++)
    {
        const auto tile = _sorted_tiles.at(tile_index);

        if (tile->uses > 0)
        {
            for (auto handle : tile->unique_attributions)
            {
                attributions->push_back(handle);
            }
        }
    }
}

void RasterMergeGroup::update_requested_tiles(
    std::span<const std::span<const RequestedTileCoords>> requested_tiles,
    size_t requested_tiles_hash,
    TileRequestOrigin allowed_tile_request_origins,
    IRasterCollection* collection,
    AssetsLoader* al,
    JobScheduler* js)
{
    HRZ_SCOPED_SAMPLE("planet raster update requested tiles");

    if (_requested_tile_list_hashes.contains(requested_tiles_hash))
    {
        // The same exact list of tiles has been passed recently,
        // ignore it.
        // In the cases where updating the tiles has an effect on
        // which tiles are requested later-on, i.e. with DTM tiles
        // for example.
        // Sometimes a given list of loaded tiles creates a situation
        // where their feedback generates another list of tiles to
        // load. When they are loaded, and a new feedback is per-
        // formed, the list of tiles that is returned is the original
        // one.
        // This loop prevents the situation from ever stabilising,
        // as long as the camera doesn't move. (Which is precisely
        // when we'd like to nothing to load.)
        // By discarding a list of requested tiles if it has already
        // requested recently, these loops are broken.
        return;
    }
    _requested_tile_list_hashes.push(requested_tiles_hash);

    if (_rasters.empty())
    {
        if (_tiles.empty()) return;

        requested_tiles = {};
    }

    // Reset use counts for all previously requested tiles.
    for (auto& tile : _tiles)
    {
        constexpr float decay_factor = 0.75f;
        tile.second.past_uses =
            std::max((int)(tile.second.past_uses * decay_factor), tile.second.uses);
        tile.second.uses = 0;
    }

    // Update use counts for requested tiles, and insert new tiles
    // or restart previously evicted tiles. (Not more than can fit
    // in the atlas.)
    // This relies on requested tiles being sorted by use count.
    size_t updated_or_inserted_tiles = 0;
    for (const auto& requested_tile_span : requested_tiles)
    {
        for (const auto& requested_tile : requested_tile_span)
        {
            if ((requested_tile.origin & allowed_tile_request_origins) == 0)
            {
                continue;
            }

            auto it = _tiles.find(requested_tile.coords);
            if (it != _tiles.end())
            {
                auto& composed_tile = it->second;
                composed_tile.uses = requested_tile.uses;

                if (composed_tile.status == ComposedTile::Status::Evicted
                    && updated_or_inserted_tiles < _num_slots_in_atlas)
                {
                    composed_tile.status = ComposedTile::Status::Reprojected;
                }

                updated_or_inserted_tiles += 1;
            }
            else if (updated_or_inserted_tiles < _num_slots_in_atlas)
            {
                ComposedTile composed_tile;
                composed_tile.coords = requested_tile.coords;
                composed_tile.uses = requested_tile.uses;
                composed_tile.past_uses = 0;
                composed_tile.status = ComposedTile::Status::WaitingForProvider;
                composed_tile.in_atlas = false;

                for (const auto& raster_pair : _rasters)
                {
                    ComposedTile::TileRaster tile_raster;
                    tile_raster.status = ComposedTile::TileRaster::Status::WaitingForProvider;
                    tile_raster.raster_id = raster_pair.id;

                    composed_tile.rasters.push_back(tile_raster);
                }

                _tiles.insert({composed_tile.coords, std::move(composed_tile)});

                updated_or_inserted_tiles += 1;
            }
        }
    }

    // Delete tiles that are not in the atlas and are unused.
    for (auto it = _tiles.begin(); it != _tiles.end();)
    {
        auto& tile = it->second;

        if (!tile.in_atlas && tile.uses == 0)
        {
            cancel_jobs_and_release_tiles(collection, js, tile);
            _tiles.erase(it++);
            continue;
        }

        it++;
    }

    // Sort the tiles from the most important one (highest LOD that covers the
    // screen), loaded first, to the least important one, loaded last.
    _sorted_tiles.clear();
    for (auto& pair : _tiles)
    {
        _sorted_tiles.push_back(&pair.second);
    }
    std::ranges::sort(
        _sorted_tiles,
        [](const ComposedTile* t1, const ComposedTile* t2)
        {
            return tile_usage_comp(
                t1->coords, t1->uses, t1->past_uses, t2->coords, t2->uses, t2->past_uses);
        });

    // Touch tiles that are already in the atlas and must stay there.
    for (unsigned int tile_index = 0;
         tile_index < _sorted_tiles.size() && tile_index < _num_slots_in_atlas; tile_index++)
    {
        const auto tile = _sorted_tiles.at(tile_index);

        if (tile->in_atlas)
        {
            _page_cache->touch(tile->coords);
        }
    }
}

uint128 RasterMergeGroup::compute_rasters_hash(
    std::span<const CollectionRasterReference> rasters) const
{
    return murmur3_x64_128(std::as_bytes(rasters));
}

void RasterMergeGroup::set_rasters(
    IRasterCollection* collection,
    JobScheduler* js,
    std::span<const CollectionRasterReference> new_rasters,
    uint32_t scene_views_bitset)
{
    _scene_views_bitset = scene_views_bitset;

    uint128 new_rasters_hash = compute_rasters_hash(new_rasters);
    if (new_rasters_hash == _rasters_hash) return;

    hrz::flat_hash_map<uint64_t, size_t> old_id_to_index_copy;
    for (size_t i = 0; i < _rasters.size(); ++i)
    {
        old_id_to_index_copy[_rasters[i].id] = i;
    }

    std::vector<ComposedTile::TileRaster> new_tile_rasters;

    for (auto& it : _tiles)
    {
        auto& tile = it.second;

        if (tile.status == ComposedTile::Status::Composing)
        {
            hrz_jobs::cancel_job(js, tile.compose_tile_ticket);
        }
        else if (tile.status == ComposedTile::Status::Compressing)
        {
            hrz_jobs::cancel_job(js, tile.compress_tile_ticket);
        }
        else if (tile.status == ComposedTile::Status::Composed)
        {
            tile.image = {};
        }

        new_tile_rasters.resize(new_rasters.size());

        // We copy the mapping because we'll use this to detect rasters that
        // have been removed: we remove the rasters that have been just moved,
        // and the remaining ones have been removed.
        hrz::flat_hash_map<uint64_t, size_t> old_id_to_index = old_id_to_index_copy;

        for (size_t new_raster_index = 0; new_raster_index < new_rasters.size(); ++new_raster_index)
        {
            bool stayed_the_same = false;

            auto new_raster_ref = new_rasters[new_raster_index];
            auto it = old_id_to_index.find(new_raster_ref.id);
            if (it != old_id_to_index.end()) // The ID was the same...
            {
                // ... but has the unique ID changed?
                stayed_the_same = (_rasters[it->second].unique_id == new_raster_ref.unique_id);
            }

            if (stayed_the_same)
            {
                // The raster just stayed in place
                new_tile_rasters[new_raster_index].swap(tile.rasters[it->second]);
                old_id_to_index.erase(it);
            }
            else
            {
                // The raster was just added
                ComposedTile::TileRaster tile_raster;
                tile_raster.status = ComposedTile::TileRaster::Status::WaitingForProvider;
                tile_raster.raster_id = new_raster_ref.id;
                new_tile_rasters[new_raster_index].swap(tile_raster);
            }
        }

        // Now the remaining rasters have been removed
        for (const auto& raster_pair : old_id_to_index)
        {
            auto& tile_raster = tile.rasters[raster_pair.second];
            cancel_jobs_and_release_tiles(collection, js, tile_raster, true);
        }

        tile.rasters.swap(new_tile_rasters);

        tile.status = (tile.coords == TileCoords{0, 0, 0})
            ? ComposedTile::Status::WaitingForProvider
            : ComposedTile::Status::Outdated;
    }

    _rasters_hash = new_rasters_hash;
    _rasters.clear();
    _rasters.reserve(new_rasters.size());
    _rasters.insert(_rasters.end(), new_rasters.begin(), new_rasters.end());
    restart_tiles(collection, js);
    _revision += 1;

    _requested_tile_list_hashes.clear();
}

// Restart the loading process for one raster.
// This forces re-obtaining the tiles from the provider, getting a newer
// version if relevant.
// Also useful if the display bounds of the raster have changed.
void RasterMergeGroup::invalidate_raster_tiles(
    IRasterCollection* collection,
    uint64_t raster_id,
    JobScheduler* js)
{
    for (auto& pair : _tiles)
    {
        auto& tile = pair.second;

        for (auto& tile_raster : tile.rasters)
        {
            if (tile_raster.raster_id == raster_id)
            {
                if (tile_raster.status == ComposedTile::TileRaster::Status::LoadingTiles)
                {
                    auto* raster = collection->get_raster_by_id(tile_raster.raster_id);
                    assert(raster);
                    if (!raster) continue;

                    for (auto& reprojected_tile : tile_raster.reprojected_tiles)
                    {
                        raster->provider->release_tile(reprojected_tile.lock_ticket, true);
                        reprojected_tile.lock_ticket = RasterProvider::LockTicket::Invalid;
                    }

                    tile_raster.reprojected_tiles.clear();
                    tile_raster.status = ComposedTile::TileRaster::Status::Reprojecting;
                }

                if (tile_raster.status == ComposedTile::TileRaster::Status::Reprojecting)
                {
                    hrz_jobs::cancel_job(js, tile_raster.reproject_tile_ticket);
                }

                tile_raster.fully_composed = false;
                tile_raster.status = ComposedTile::TileRaster::Status::WaitingForProvider;

                if (tile.status == ComposedTile::Status::Composed)
                {
                    tile.image = {};
                }

                tile.status = (tile.coords == TileCoords{0, 0, 0})
                    ? ComposedTile::Status::WaitingForProvider
                    : ComposedTile::Status::Outdated;

                break;
            }
        }
    }

    _requested_tile_list_hashes.clear();

    if (_bounds_tracker.has_value())
    {
        _bounds_tracker->clear_and_increment_version();
    }
}

// Restart the loading process for the tiles.
// Use this if a raster has been added or removed,
// or if the composition parameters have changed.
// This does not affect the individual tile rasters.
void RasterMergeGroup::restart_tiles(IRasterCollection* collection, JobScheduler* js)
{
    for (auto& pair : _tiles)
    {
        auto& tile = pair.second;

        if (tile.status == ComposedTile::Status::Evicted)
        {
            continue;
        }

        if (tile.status == ComposedTile::Status::Composing)
        {
            hrz_jobs::cancel_job(js, tile.compose_tile_ticket);
        }
        else if (tile.status == ComposedTile::Status::Compressing)
        {
            hrz_jobs::cancel_job(js, tile.compress_tile_ticket);
        }

        for (auto& tile_raster : tile.rasters)
        {
            if (tile_raster.status == ComposedTile::TileRaster::Status::LoadingTiles)
            {
                auto* raster = collection->get_raster_by_id(tile_raster.raster_id);
                assert(raster);

                if (!raster || !raster->is_visible
                    || !is_visible_in(raster->visibility_constraints_result))
                {
                    for (auto& reprojected_tile : tile_raster.reprojected_tiles)
                    {
                        raster->provider->release_tile(reprojected_tile.lock_ticket, true);
                        reprojected_tile.lock_ticket = RasterProvider::LockTicket::Invalid;
                    }
                    tile_raster.reprojected_tiles.clear();
                    tile_raster.status = ComposedTile::TileRaster::Status::Reprojecting;
                }
            }

            if (tile_raster.status == ComposedTile::TileRaster::Status::Reprojecting)
            {
                hrz_jobs::cancel_job(js, tile_raster.reproject_tile_ticket);
            }

            if (tile.status == ComposedTile::Status::Composed)
            {
                tile.image = {};
            }
        }

        tile.status = (tile.coords == TileCoords{0, 0, 0})
            ? ComposedTile::Status::WaitingForProvider
            : ComposedTile::Status::Outdated;
    }

    if (_bounds_tracker.has_value())
    {
        _bounds_tracker->clear_and_increment_version();
    }
}

void RasterMergeGroup::work(
    IRasterCollection* collection,
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js)
{
    HRZ_SCOPED_SAMPLE("planet raster work");

    auto is_beyond =
        [](ComposedTile::TileRaster::Status actual, ComposedTile::TileRaster::Status reference)
    { return (int)actual > (int)reference; };

    //@Todo check local tiling when HRZ-199 is done.
    auto is_visible = [&](const Raster* raster, const ComposedTile::ReprojectedTile& rt,
                          const TileCoords& composed_coords)
    {
        // Do the visibility test only for EPSG:3857 tiled imagery rasters.
        if (auto image_format = raster->provider->get_image_format();
            image_format == HrzProtocol::ImageFormat::SIGNED_FIXED_24_8
            || image_format == HrzProtocol::ImageFormat::R_F32
            || image_format == HrzProtocol::ImageFormat::R_F32_SILICIUM
            || image_format == HrzProtocol::ImageFormat::TERRARIUM
            || image_format == HrzProtocol::ImageFormat::TERRAIN_RGB
            || raster->provider->get_raster_provider_type()
                == HrzProtocol::RasterProviderType::UNTILED_RASTER_PROVIDER
            || rt.mesh.has_value()) // This means it is not an EPSG:3857 projection.
        {
            return true;
        }

        // For EPSG:3857 rasters a reprojected tile is considered visible if:
        // * It covers the centre part of the composed tile (even partially) or,
        // * It is on the border but is already getting downloaded for another
        //   composed tile. In this case we can use it for free for this composed
        //   tile, in order to allow filtering the border.
        // In other cases, we avoid downloading tiles just for the border, so we
        // don't consider the reprojected tile visible.
        // This is possible because there is dedicated support for EPSG:3857 missing
        // tiles on the borders later on.

        auto sign = [](int32_t n) { return n > 0 ? 1 : (n < 0 ? -1 : 0); };

        const TileCoords& rt_coords = rt.coords;

        if (rt_coords == composed_coords)
        {
            // The reprojected tile matches perfectly with the composed tile,
            // so it has to be marked as visible.
            return true;
        }

        if (rt_coords.lod <= 2)
        {
            // Always download tiles at the lowest levels.
            return true;
        }

        int8_t lod_diff = composed_coords.lod - rt_coords.lod;
        if (lod_diff == 0)
        {
            // This reprojected tile is on the border of the composed tile,
            // at the same level.
            // Consider it visible if it is already loaded for its matching
            // composed tile.
            return _tiles.find(rt_coords) != _tiles.end();
        }
        else if (lod_diff > 0)
        {
            // The reprojected tile is larger (i.e. has a lower LOD) than the
            // tile being composed.
            // We have gone beyond the raster's max LOD.

            uint32_t shifted_composed_x = composed_coords.x >> lod_diff;
            uint32_t shifted_composed_y = composed_coords.y >> lod_diff;

            if (shifted_composed_x == rt_coords.x && shifted_composed_y == rt_coords.y)
            {
                // This reprojected tile covers the centre part of the composed tile.
                return true;
            }
            else
            {
                // Some tests are not technically correct near the antimeridian, but it should not
                // matter.
                uint32_t neighbor_x =
                    composed_coords.x + sign((int32_t)rt_coords.x - (int32_t)shifted_composed_x);
                uint32_t neighbor_y =
                    composed_coords.y + sign((int32_t)rt_coords.y - (int32_t)shifted_composed_y);

                // If we are also composing the neighbouring tile whose reprojected tile at its
                // centre is the reprojected tile at hand, we can consider it visible.
                return _tiles.find({neighbor_x, neighbor_y, composed_coords.lod}) != _tiles.end();
            }
        }
        else // lod_diff < 0
        {
            // The reprojected tile is smaller (i.e. has a higher LOD) than the
            // tile being composed.
            // We have not yet reached the raster's min LOD.

            uint32_t shifted_rt_x = rt_coords.x >> -lod_diff;
            uint32_t shifted_rt_y = rt_coords.y >> -lod_diff;

            if (shifted_rt_x == composed_coords.x && shifted_rt_y == composed_coords.y)
            {
                // This reprojected tile covers a part of the centre of the composed tile.
                return true;
            }
            else
            {
                // If we are also reprojected the neighbouring tile that has the reprojected
                // tile at hand in its centre part, we can consider it visible.
                return _tiles.find({shifted_rt_x, shifted_rt_y, composed_coords.lod})
                    != _tiles.end();
            }
        }

        // There already is a composed tile covering exactly the same area.
        return true;
    };

    for (uint32_t tile_index = 0; tile_index < _sorted_tiles.size(); ++tile_index)
    {
        auto& tile = *_sorted_tiles[tile_index];

        if (tile.status == ComposedTile::Status::WaitingForProvider)
        {
            bool all_providers_ready = true;

            for (auto& tile_raster : tile.rasters)
            {
                auto* raster = collection->get_raster_by_id(tile_raster.raster_id);
                assert(raster);
                if (!raster || !raster->is_visible
                    || !is_visible_in(raster->visibility_constraints_result))
                {
                    continue;
                }

                if (tile_raster.status == ComposedTile::TileRaster::Status::Reprojecting)
                {
                    // We need to check the ticket validity to ensure that a tile_raster in
                    // reprojection doesn't get stuck in this state. Indeed, if the reprojection job
                    // has been canceled for any reason (e.g. with `restart_tiles()`) then the job
                    // will never be recreated and the tile_raster will remain in reprojection
                    // indefinitely.
                    if (hrz_jobs::is_job_valid(js, tile_raster.reproject_tile_ticket))
                    {
                        continue;
                    }
                }
                else if (is_beyond(
                             tile_raster.status,
                             ComposedTile::TileRaster::Status::WaitingForProvider))
                {
                    // Provider is already ready
                    continue;
                }

                auto provider_status = raster->provider->get_status();

                if (provider_status == RasterProvider::Status::Ready)
                {
                    if (lm::intersect(
                            hrz::mercator_tile_bbox_meters(tile.coords), raster->display_bounds))
                    {
                        tile_raster.reproject_tile_ticket =
                            create_reprojection_job(tile.coords, raster, js);

                        tile_raster.status = ComposedTile::TileRaster::Status::Reprojecting;
                    }
                    else
                    {
                        // The planet tile isn't in the display bounds of the raster,
                        // so no raster tiles are reprojected.
                        tile_raster.reprojected_tiles.clear();
                        tile_raster.status = ComposedTile::TileRaster::Status::Reprojected;
                    }
                }
                else if (provider_status == RasterProvider::Status::Error)
                {
                    // If the provider is in error, it is considered as "ready"
                    // wrt its initialisation because there is no need to wait
                    // on it any longer.
                    // As it won't be able to provide any images, the tile raster
                    // is considered reprojected, but its reprojected tile list
                    // is empty. (This way it doesn't block the other rasters in
                    // the group.)
                    tile_raster.status = ComposedTile::TileRaster::Status::Reprojected;
                }
                else
                {
                    all_providers_ready = false;
                }
            }

            if (all_providers_ready)
            {
                tile.status = ComposedTile::Status::Reprojecting;
            }
        }

        if (tile.status == ComposedTile::Status::Reprojecting)
        {
            bool all_rasters_reprojected = true;

            for (auto& tile_raster : tile.rasters)
            {
                auto* raster = collection->get_raster_by_id(tile_raster.raster_id);
                assert(raster);
                if (!raster || !raster->is_visible
                    || !is_visible_in(raster->visibility_constraints_result))
                {
                    continue;
                }

                if (is_beyond(tile_raster.status, ComposedTile::TileRaster::Status::Reprojecting))
                {
                    // Raster is already reprojected
                    continue;
                }
                else if (
                    hrz_jobs::is_job_valid(js, tile_raster.reproject_tile_ticket)
                    && hrz_jobs::is_job_finished(js, tile_raster.reproject_tile_ticket))
                {
                    // Reprojection job has just finished
                    HRZ_SCOPED_SAMPLE("planet raster work load");

                    assert(tile.status == ComposedTile::Status::Reprojecting);

                    if (hrz_jobs::get_job_status(js, tile_raster.reproject_tile_ticket)
                        == hrz::job_scheduler::JobStatus::Finished_Success)
                    {
                        tile_raster.reprojected_tiles.clear();

                        ReprojectedTiles reprojected_tiles;
                        hrz_jobs::get_job_response(
                            js, tile_raster.reproject_tile_ticket, reprojected_tiles);

                        for (const auto& reprojected_tile : reprojected_tiles.tiles)
                        {
                            ComposedTile::ReprojectedTile rt;
                            rt.coords = reprojected_tile.coords;
                            rt.lock_ticket = RasterProvider::LockTicket::Invalid;

                            if (reprojected_tile.grid_coords.size() != 0)
                            {
                                ComposedTile::ReprojectedTile::Mesh mesh;
                                mesh.grid_size.x = reprojected_tile.grid_size.x;
                                mesh.grid_size.y = reprojected_tile.grid_size.y;

                                auto coords_count = reprojected_tile.grid_coords.size() / 2;
                                assert(coords_count >= 4);
                                assert(mesh.grid_size.x >= 2);
                                assert(mesh.grid_size.y >= 2);
                                assert(coords_count == mesh.grid_size.x * mesh.grid_size.y);

                                mesh.grid.reserve(coords_count);

                                for (size_t i = 0; i < coords_count; ++i)
                                {
                                    lm::vec2 pos(
                                        reprojected_tile.grid_coords.at(i * 2 + 0),
                                        reprojected_tile.grid_coords.at(i * 2 + 1));
                                    mesh.grid.push_back(pos);
                                }
                                rt.mesh.emplace(mesh);
                            }

                            rt.uv_clip = reprojected_tile.uv_clip;
                            tile_raster.reprojected_tiles.push_back(std::move(rt));
                        }

                        std::ranges::sort(
                            tile_raster.reprojected_tiles,
                            [](const ComposedTile::ReprojectedTile& p0,
                               const ComposedTile::ReprojectedTile& p1)
                            { return p0.coords.lod < p1.coords.lod; });

                        tile_raster.status = ComposedTile::TileRaster::Status::LoadingTiles;
                    }
                    else
                    {
                        // Reprojection failed. Leave the tile empty.
                        tile_raster.status = ComposedTile::TileRaster::Status::Reprojected;
                    }
                }
                else
                {
                    // Reprojection job is still ongoing
                    all_rasters_reprojected = false;
                }
            }

            if (all_rasters_reprojected)
            {
                tile.status = ComposedTile::Status::Reprojected;
            }
        }

        if (tile.status == ComposedTile::Status::Reprojected)
        {
            // Request images and lock them until they are sent to the composition job.
            for (auto& tile_raster : tile.rasters)
            {
                auto* raster = collection->get_raster_by_id(tile_raster.raster_id);
                assert(raster);
                if (!raster || !raster->is_visible
                    || !is_visible_in(raster->visibility_constraints_result))
                {
                    continue;
                }

                for (auto& rt : tile_raster.reprojected_tiles)
                {
                    auto make_request = [&]()
                    {
                        assert(rt.lock_ticket == RasterProvider::LockTicket::Invalid);
                        rt.lock_ticket = raster->provider->request_and_lock_tile(
                            rt.coords, al,
                            compute_tile_loading_priority(raster->loading_priority, tile_index));
                    };

                    bool tile_is_visible = is_visible(raster, rt, tile.coords);
                    if (!rt.was_visible && tile_is_visible)
                    {
                        rt.was_visible = true;
                        make_request();
                    }
                    else if (rt.was_visible && !tile_is_visible)
                    {
                        rt.was_visible = false;
                        raster->provider->release_tile(rt.lock_ticket);
                        rt.lock_ticket = RasterProvider::LockTicket::Invalid;
                    }

                    if (rt.was_visible && rt.lock_ticket == RasterProvider::LockTicket::Invalid)
                    {
                        make_request();
                    }
                }
            }

            tile.status = ComposedTile::Status::LoadingTiles;
        }

        if (tile.status == ComposedTile::Status::LoadingTiles)
        {
            HRZ_SCOPED_SAMPLE_A("planet raster work check load");

            bool all_rasters_loaded = true;

            for (auto& tile_raster : tile.rasters)
            {
                auto* raster = collection->get_raster_by_id(tile_raster.raster_id);
                assert(raster);
                if (!raster || !raster->is_visible
                    || !is_visible_in(raster->visibility_constraints_result))
                {
                    continue;
                }

                bool all_raster_tiles_loaded = true;

                for (auto& reprojected_tile : tile_raster.reprojected_tiles)
                {
                    if (!reprojected_tile.was_visible) continue;

                    auto status = raster->provider->get_tile_status(reprojected_tile.lock_ticket);

                    if (status != RasterProvider::TileStatus::Loaded)
                    {
                        all_raster_tiles_loaded = false;
                        break;
                    }
                }

                if (all_raster_tiles_loaded)
                {
                    tile_raster.status = ComposedTile::TileRaster::Status::Loaded;
                }
                else
                {
                    all_rasters_loaded = false;
                    break;
                }
            }

            if (all_rasters_loaded)
            {
                tile.status = ComposedTile::Status::Loaded;
            }
        }

        if (tile.status == ComposedTile::Status::Loaded)
        {
            HRZ_SCOPED_SAMPLE("planet raster work prepare compose");

            RasterTileCompositionParams job_params;
            job_params.output_coords = tile.coords;
            job_params.output_format = _composed_image_format;

            tile.unique_attributions.clear();

            for (auto& tile_raster : tile.rasters)
            {
                auto* raster = collection->get_raster_by_id(tile_raster.raster_id);
                assert(raster);
                if (!raster || !raster->is_visible
                    || !is_visible_in(raster->visibility_constraints_result))
                {
                    continue;
                }

                bool fully_composed = true;

                for (const auto& rt : tile_raster.reprojected_tiles)
                {
                    if (!rt.was_visible || rt.lock_ticket == RasterProvider::LockTicket::Invalid)
                    {
                        fully_composed = false;
                        continue;
                    }

                    assert(
                        raster->provider->get_tile_status(rt.lock_ticket)
                        == RasterProvider::TileStatus::Loaded);

                    RasterProvider::TileImage tile_image =
                        raster->provider->get_tile_image(rt.lock_ticket);
                    if (tile_image.image.valid())
                    {
                        RasterTileCompositionParams::ImageWithCanvas img_canvas;
                        img_canvas.image = std::move(tile_image.image);

                        if (rt.mesh.has_value())
                        {
                            auto& mesh = rt.mesh.value();
                            RasterTileCompositionParams::ReprojectionMesh param_mesh;

                            param_mesh.quad_count.x = mesh.grid_size.x - 1;
                            param_mesh.quad_count.y = mesh.grid_size.y - 1;
                            param_mesh.uv_clip = rt.uv_clip;

                            param_mesh.tile_coords = rt.coords;
                            param_mesh.tile_image_coords = tile_image.coords;

                            auto& grid = param_mesh.grid;
                            grid.resize(mesh.grid.size() * 2, 0.0f);
                            for (unsigned int i = 0; i < mesh.grid.size(); ++i)
                            {
                                grid[i * 2 + 0] = mesh.grid[i].x;
                                grid[i * 2 + 1] = mesh.grid[i].y;
                            }

                            img_canvas.canvas = param_mesh;
                        }
                        else
                        {
                            RasterTileCompositionParams::Blit proto;

                            proto.input_coords = tile_image.coords;

                            TileToTileUvTransform<float> uv_xform(rt.coords, tile_image.coords);
                            lm::bbox2 uv_clip = rt.uv_clip;
                            uv_clip.min = uv_xform(uv_clip.min);
                            uv_clip.max = uv_xform(uv_clip.max);
                            proto.uv_clip = uv_clip;

                            img_canvas.canvas = proto;
                        }

                        // Web Mercator +y is towards the North, but UV +y is towards the South.
                        auto tile_bbox = hrz::mercator_tile_bbox_meters(tile.coords);
                        auto to_uv = [&](lm::dvec2 v)
                        {
                            auto uv = lm::vec2(lm::clamp(
                                (v - tile_bbox.min) / lm::size(tile_bbox), {0, 0}, {1, 1}));
                            uv.y = 1.0 - uv.y;
                            return uv;
                        };
                        auto uv_display_bounds = lm::bbox2(
                            to_uv(raster->display_bounds.min), to_uv(raster->display_bounds.max));
                        std::swap(uv_display_bounds.min.y, uv_display_bounds.max.y);
                        img_canvas.dst_uv_clip = uv_display_bounds;

                        img_canvas.sampling = raster->sampling;
                        img_canvas.blending = raster->blending;
                        img_canvas.nodata = raster->provider->get_nodata();

                        job_params.images.push_back(std::move(img_canvas));

                        for (auto attribution : tile_image.attribution)
                        {
                            tile.unique_attributions.push_back(attribution);
                        }
                    }
                }

                tile_raster.fully_composed = fully_composed;
            }

            job_params.compute_value_bounds = _bounds_tracker.has_value();
            if (_bounds_tracker.has_value())
            {
                job_params.pixel_to_value = _bounds_tracker.value().pixel_to_value;
            }

            tile.compose_tile_ticket = hrz_jobs::add_job_compose_raster_tile(
                js, job_params, {monitoring::systems::PlanetSurface});
            tile.status = ComposedTile::Status::Composing;

            // The sources images have been copied to the composition job.
            // They can now be unlocked in the provider.
            for (auto& tile_raster : tile.rasters)
            {
                auto* raster = collection->get_raster_by_id(tile_raster.raster_id);
                assert(raster);
                if (!raster) continue;

                for (auto& reprojected_tile : tile_raster.reprojected_tiles)
                {
                    raster->provider->release_tile(reprojected_tile.lock_ticket);
                    reprojected_tile.lock_ticket = RasterProvider::LockTicket::Invalid;
                }
            }
        }

        if (tile.status == ComposedTile::Status::Composing)
        {
            HRZ_SCOPED_SAMPLE("planet raster work retrieve composed");

            if (hrz_jobs::is_job_valid(js, tile.compose_tile_ticket)
                && hrz_jobs::is_job_finished(js, tile.compose_tile_ticket))
            {
                if (hrz_jobs::get_job_status(js, tile.compose_tile_ticket)
                    == job_scheduler::JobStatus::Finished_Success)
                {
                    RasterTileCompositionResponse response;
                    hrz_jobs::get_job_response(js, tile.compose_tile_ticket, response);

                    assert(
                        response.image.width() == ATLAS_TILE_SIZE
                        && response.image.height() == ATLAS_TILE_SIZE);

                    response.image.register_blob_metadata(
                        ba, "response.image type"_ss, "composed tile"_ss);
                    response.image.register_blob_metadata(
                        ba, "coords"_ss,
                        fmt::format("{}-{}-{}", tile.coords.lod, tile.coords.x, tile.coords.y));
                    response.image.register_blob_metadata(ba, "raster group"_ss, _name);

                    response.image.register_blob_owner(ba, {monitoring::systems::PlanetSurface});

                    if (my::is_format_compressed(_atlas_image_format)
                        && !my::is_format_compressed(
                            hrz::image_format_to_gpu_format(_composed_image_format)))
                    {
                        hrz::BlobImageCompressionParams job_params;
                        job_params.image = std::move(response.image);
                        job_params.output_format = _atlas_image_format;
                        tile.compress_tile_ticket = hrz_jobs::add_job_compress_blob_image(
                            js, job_params, {monitoring::systems::PlanetSurface});
                        tile.status = ComposedTile::Status::Compressing;
                    }
                    else
                    {
                        tile.image = std::move(response.image);
                        tile.status = ComposedTile::Status::Composed;
                    }

                    if (_bounds_tracker.has_value())
                    {
                        if (response.min_value.has_value() && response.max_value.has_value())
                        {
                            _bounds_tracker.value().values.insert_or_assign(
                                tile.coords,
                                {response.min_value.value(), response.max_value.value()});
                        }

                        if (response.min_value.has_value())
                        {
                            _bounds_tracker->min_value =
                                std::min(_bounds_tracker->min_value, response.min_value.value());
                        }
                        if (response.max_value.has_value())
                        {
                            _bounds_tracker->max_value =
                                std::max(_bounds_tracker->max_value, response.max_value.value());
                        }
                    }
                }
                else
                {
                    tile.status = ComposedTile::Status::Error;
                }
            }
        }

        if (tile.status == ComposedTile::Status::Compressing)
        {
            HRZ_SCOPED_SAMPLE("planet raster work retrieve compressed");

            if (hrz_jobs::is_job_valid(js, tile.compress_tile_ticket)
                && hrz_jobs::is_job_finished(js, tile.compress_tile_ticket))
            {
                if (hrz_jobs::get_job_status(js, tile.compress_tile_ticket)
                    == job_scheduler::JobStatus::Finished_Success)
                {
                    hrz::BlobImage image;
                    hrz_jobs::get_job_response(js, tile.compress_tile_ticket, image);
                    tile.image = std::move(image);

                    tile.status = ComposedTile::Status::Composed;
                }
                else
                {
                    tile.status = ComposedTile::Status::Error;
                }
            }
        }

        if (tile.status == ComposedTile::Status::Outdated)
        {
            _page_cache->evict_page(tile.coords);

            tile.in_atlas = false;
            tile.status = ComposedTile::Status::WaitingForProvider;
        }

        if (tile.status == ComposedTile::Status::Displayed)
        {
            bool need_update = false;
            for (auto& tile_raster : tile.rasters)
            {
                auto* raster = collection->get_raster_by_id(tile_raster.raster_id);
                assert(raster);
                if (!raster || !raster->is_visible
                    || !is_visible_in(raster->visibility_constraints_result))
                {
                    continue;
                }

                if (!tile_raster.fully_composed)
                {
                    for (auto& rt : tile_raster.reprojected_tiles)
                    {
                        if (!rt.was_visible && is_visible(raster, rt, tile.coords))
                        {
                            need_update = true;
                            break;
                        }
                    }
                }

                if (need_update)
                {
                    break;
                }
            }

            if (need_update)
            {
                tile.status = ComposedTile::Status::Reprojected;
            }
        }
    }

    // Work on the fetch requests
    if (!_incomplete_fetch_requests.empty())
    {
        for (auto it = _incomplete_fetch_requests.begin(); it != _incomplete_fetch_requests.end();)
        {
            auto ticket = *it;
            auto* request = _fetch_request_pool.get_object(ticket);

            auto* raster = collection->get_raster_by_id(request->raster_id);
            if (!raster)
            {
                _incomplete_fetch_requests.erase(it++);
                _completed_fetch_requests.insert(ticket);
                continue;
            }

            // The data hasn't been requested yet
            if (request->lock == RasterProvider::SourceLockTicket::Invalid)
            {
                request->lock = raster->provider->request_and_lock_source_tile(request->tile, al);
                if (request->lock == RasterProvider::SourceLockTicket::Invalid)
                {
                    _incomplete_fetch_requests.erase(it++);
                    _completed_fetch_requests.insert(ticket);
                    continue;
                }
            }
            else
            {
                auto status = raster->provider->get_source_tile_status(request->lock);
                if (status == RasterProvider::TileStatus::Loading)
                {
                    ++it;
                    continue;
                }
                else
                {
                    // The requested data is ready
                    auto geometry = raster->provider->get_geometry();

                    pl_Crs crs;
                    bool convert_success = hrz::convert_crs(geometry.projection, &crs);
                    if (!convert_success)
                    {
                        HRZ_LOG_WARNING("Couldn't convert raster projection string to pl_Crs.");

                        raster->provider->release_source_tile(request->lock);

                        _incomplete_fetch_requests.erase(it++);
                        _completed_fetch_requests.insert(ticket);
                        continue;
                    }

                    ImageTilingInfo info = compute_image_tiling_info(geometry, &crs);

                    auto raster_result_opt = get_tile_image_pixel(
                        raster, request->lock, request->tile, request->pixel_pos, info);

                    if (raster_result_opt.has_value())
                    {
                        // `pixel_pos` in the fetch request data is the pixel position modulo
                        // the tile size. To match the data returned in picking messages, we want
                        // to return the original pixel position instead.
                        const lm::dvec2 pixel_pos_raster =
                            pixel_pos_from_proj_pos(info, request->proj_pos.xy);

                        request->result.mutable_layer()->mutable_handle()->set_opaque(raster->id);
                        request->result.mutable_layer()->set_type(raster->type);
                        raster_result_opt->mutable_raster_position()->set_x(request->proj_pos.x);
                        raster_result_opt->mutable_raster_position()->set_y(request->proj_pos.y);
                        raster_result_opt->mutable_pixel_position()->set_x(pixel_pos_raster.x);
                        raster_result_opt->mutable_pixel_position()->set_y(pixel_pos_raster.y);
                        request->result.mutable_raster()->CopyFrom(raster_result_opt.value());
                    }

                    raster->provider->release_source_tile(request->lock);

                    _incomplete_fetch_requests.erase(it++);
                    _completed_fetch_requests.insert(ticket);
                    continue;
                }
            }
            ++it;
        }
    }
}

bool RasterMergeGroup::is_working(const IRasterCollection* collection) const
{
    for (uint32_t tile_index = 0;
         tile_index < _sorted_tiles.size() && tile_index < _num_slots_in_atlas; ++tile_index)
    {
        const auto& tile = *_sorted_tiles[tile_index];
        if (tile.status != ComposedTile::Status::Displayed
            && tile.status != ComposedTile::Status::Evicted
            && tile.status != ComposedTile::Status::Error)
        {
            for (const auto& tile_raster : tile.rasters)
            {
                const auto* raster = collection->get_raster_by_id(tile_raster.raster_id);
                assert(raster);
                if (raster && raster->is_visible
                    && is_visible_in(raster->visibility_constraints_result))
                {
                    return true;
                }
            }
        }
    }

    return false;
}

bool RasterMergeGroup::upload_composed_tiles_and_bake_clipmap()
{
    bool page_cache_updated = false;

    // Do not insert more tiles than can fit in the atlas.
    for (unsigned int tile_index = 0;
         tile_index < _sorted_tiles.size() && tile_index < _num_slots_in_atlas; tile_index++)
    {
        auto& tile = *_sorted_tiles.at(tile_index);

        if (tile.status != ComposedTile::Status::Composed) continue;

        TileCoords tile_coords;
        tile_coords.x = tile.coords.x;
        tile_coords.y = tile.coords.y;
        tile_coords.lod = tile.coords.lod;

        auto upload_result = _page_cache->upload_page(std::move(tile.image), tile_coords);

        if (upload_result.evicted)
        {
            auto evicted_tile_coords = upload_result.evicted_tile;
            auto it = _tiles.find(evicted_tile_coords);

            if (it != _tiles.end())
            {
                auto& evicted_tile = _tiles.at(evicted_tile_coords);

                assert(evicted_tile.in_atlas);
                evicted_tile.in_atlas = false;

                evicted_tile.status = ComposedTile::Status::Evicted;
            }
            else
            {
                HRZ_LOG_WARNING("Evicted tile ({}) not in tile list", evicted_tile_coords);
            }
        }

        tile.image = {};
        tile.in_atlas = true;
        tile.status = ComposedTile::Status::Displayed;

        page_cache_updated = true;
    }

    if (page_cache_updated)
    {
        _page_cache->bake_clipmap();
    }

    return page_cache_updated;
}

std::pair<TileCoords, RasterProvider::SourceLockTicket> find_most_detailed_tile_available(
    const Raster* raster,
    const hrz::ImageTilingInfo& tiling_info,
    const lm::dvec3& projected_coords,
    lm::ivec2* out_pixel)
{
    // Get picked tile at the most detailed level and search from here.
    lm::dvec2 domain_coords = {
        (projected_coords.x - tiling_info.domain_bounds.min.x) / tiling_info.domain_bounds_size.x,
        (tiling_info.domain_bounds.max.y - projected_coords.y) / tiling_info.domain_bounds_size.y,
    };

    lm::dvec2 domain_coords_pixel(lm::floor(domain_coords * tiling_info.domain_pixel_size));
    hrz::TileCoords tile{
        (uint32_t)std::floor(domain_coords_pixel.x / tiling_info.provider_tile_pixel_size),
        (uint32_t)std::floor(domain_coords_pixel.y / tiling_info.provider_tile_pixel_size),
        tiling_info.max_lod,
    };

    // Search the tile hierarchy bottom-up until something is found or the root is reached.

    auto& provider = raster->provider;
    do
    {
        auto lock = provider->lock_source_tile_if_ready(tile);

        if (lock != RasterProvider::SourceLockTicket::Invalid)
        {
            if (provider->get_source_tile_status(lock) == RasterProvider::TileStatus::Loaded)
            {
                auto source_image = provider->get_source_tile_image(lock);

                if (source_image.image.valid())
                {
                    *out_pixel = lm::ivec2(
                        (int32_t)domain_coords_pixel.x % tiling_info.provider_tile_pixel_size,
                        (int32_t)domain_coords_pixel.y % tiling_info.provider_tile_pixel_size);
                    return std::make_pair(tile, lock);
                }
            }
            else
            {
                provider->release_source_tile(lock);
            }
        }

        if (tile.lod == 0)
        {
            break;
        }

        tile = tile.parent();
        domain_coords_pixel *= 0.5;
    } while (tile.lod >= tiling_info.min_lod);

    return std::make_pair(TileCoords{}, RasterProvider::SourceLockTicket::Invalid);
}

void RasterMergeGroup::pick(
    IRasterCollection* collection,
    const lm::dvec3& position,
    std::span<const hrz_proto::LayerHandle> included_rasters,
    hrz_proto::PickResults& pick_results)
{
    // Picking tries to gather as much results as it can from loaded tiles that are still in
    // cache. This way we avoid losing time downloading the tile that need to be picked. It
    // allows picking results to be returned quicker but these can be inaccurate or incomplete
    // in some scenarios. If results don't please the user then it is their job to retrieve the
    // desired information, not the engine's.

    lm::dvec3 wmerc = hrz::ecef_to_web_mercator_alt(position);

    for (const auto& raster_pair : _rasters)
    {
        auto* raster = collection->get_raster_by_index(raster_pair.index);

        if (!raster->is_visible || !is_visible_in(raster->visibility_constraints_result)
            || raster->provider->get_status() != RasterProvider::Status::Ready)
            continue;

        bool included = false;
        for (const auto& layer : included_rasters)
        {
            if (raster->id == layer.opaque())
            {
                included = true;
                break;
            }
        }

        if (!included || !lm::contains(raster->display_bounds, wmerc.xy))
        {
            continue;
        }

        auto geometry = raster->provider->get_geometry();

        pl_Crs crs;
        bool convert_success = hrz::convert_crs(geometry.projection, &crs);
        if (!convert_success)
        {
            HRZ_LOG_WARNING("Couldn't convert raster projection string to pl_Crs.");
            continue;
        }

        ImageTilingInfo info = compute_image_tiling_info(geometry, &crs);

        // Compute the picked position in the projection of the dataset.
        pl_Transform transform;
        pl_bake_transform(&hrz_proj::wmerc, &crs, &transform);

        lm::dvec3 proj_pos = wmerc;
        pl_transform_in_place_canonical(&transform, 1, &proj_pos.x);

        // Compare picked location against raster bounds.
        if (!lm::contains(info.raster_bounds, proj_pos.xy))
        {
            continue;
        }

        lm::ivec2 pixel_pos;
        auto lock = find_most_detailed_tile_available(raster, info, proj_pos, &pixel_pos);

        auto raster_result_opt =
            get_tile_image_pixel(raster, lock.second, lock.first, pixel_pos, info);

        if (lock.second != RasterProvider::SourceLockTicket::Invalid)
        {
            raster->provider->release_source_tile(lock.second);
        }

        // Don't return the layer when nodata is picked.
        if (!raster_result_opt.has_value())
        {
            continue;
        }

        auto raster_result = std::move(raster_result_opt.value());

        const lm::dvec2 pixel_pos_raster = pixel_pos_from_proj_pos(info, proj_pos.xy);

        // Populate picking results for that raster.
        hrz_proto::PickLayerResult pr;
        pr.mutable_layer()->mutable_handle()->set_opaque(raster->id);
        pr.mutable_layer()->set_type(raster->type);
        raster_result.mutable_raster_position()->set_x(proj_pos.x);
        raster_result.mutable_raster_position()->set_y(proj_pos.y);
        raster_result.mutable_pixel_position()->set_x(pixel_pos_raster.x);
        raster_result.mutable_pixel_position()->set_y(pixel_pos_raster.y);
        pr.mutable_raster()->CopyFrom(raster_result);

        *(pick_results.mutable_results()->Add()) = pr;
    }
}

void RasterMergeGroup::schedule_raster_data_fetch(
    IRasterCollection* collection,
    const GeoPosition2& position,
    std::span<const hrz_proto::LayerHandle> layers,
    std::vector<RasterDataFetchMergeGroupTicket>& out_tickets)
{
    for (const auto& raster_pair : _rasters)
    {
        auto* raster = collection->get_raster_by_index(raster_pair.index);

        bool included = false;
        for (const auto& layer : layers)
        {
            if (raster->id == layer.opaque())
            {
                included = true;
                break;
            }
        }

        lm::dvec2 wmerc_pos = geo_to_web_mercator(position);
        if (!included || !lm::contains(raster->display_bounds, wmerc_pos))
        {
            continue;
        }

        auto ticket = _fetch_request_pool.alloc();
        auto* fetch = _fetch_request_pool.get_object(ticket);

        auto geometry = raster->provider->get_geometry();

        pl_Crs crs;
        bool convert_success = hrz::convert_crs(geometry.projection, &crs);
        if (!convert_success)
        {
            HRZ_LOG_WARNING("Couldn't convert raster projection string to pl_Crs.");
            continue;
        }

        ImageTilingInfo info = compute_image_tiling_info(geometry, &crs);

        // Compute the queried position in the projection of the dataset
        pl_Transform transform;
        pl_bake_transform(&hrz_proj::lonlat_rad, &crs, &transform);

        lm::dvec3 proj_pos = {position.lon, position.lat, 0};
        pl_transform_in_place_canonical(&transform, 1, &proj_pos.x);

        // Compare picked location against raster bounds.
        if (!lm::contains(info.raster_bounds, proj_pos.xy))
        {
            continue;
        }

        lm::dvec2 domain_coords_pixel = lm::floor(pixel_pos_from_proj_pos(info, proj_pos.xy));

        fetch->tile = {
            (uint32_t)std::floor(domain_coords_pixel.x / info.provider_tile_pixel_size),
            (uint32_t)std::floor(domain_coords_pixel.y / info.provider_tile_pixel_size),
            info.max_lod};

        fetch->proj_pos = proj_pos;
        fetch->pixel_pos = lm::ivec2(
            (int32_t)domain_coords_pixel.x % info.provider_tile_pixel_size,
            (int32_t)domain_coords_pixel.y % info.provider_tile_pixel_size);

        fetch->lock = RasterProvider::SourceLockTicket::Invalid;
        fetch->raster_id = raster->id;

        _incomplete_fetch_requests.insert(ticket);
        out_tickets.push_back(ticket);
    }
}

bool RasterMergeGroup::is_data_fetch_ready(RasterDataFetchMergeGroupTicket ticket) const
{
    return _completed_fetch_requests.contains(ticket);
}

hrz_proto::PickLayerResult RasterMergeGroup::get_data_fetch_result(
    RasterDataFetchMergeGroupTicket ticket)
{
    assert(_completed_fetch_requests.contains(ticket));
    auto* request = _fetch_request_pool.get_object(ticket);

    auto result = request->result;

    _fetch_request_pool.release(ticket);
    _completed_fetch_requests.erase(ticket);

    return result;
}

void RasterMergeGroup::dev_ui(
    IRasterCollection* collection,
    mu_Context* ctx,
    const char* header_title,
    uint32_t scene_views,
    uint32_t raster_groups)
{
    fmt::memory_buffer buffer;

    if (mu_header(ctx, header_title))
    {
        static int layout[] = {
            180,
            -1,
        };

        mu_layout_row(ctx, 2, layout, 0);

        unsigned int in_atlas = 0;

        for (const auto& pair : _tiles)
        {
            const auto& tile = pair.second;
            if (tile.in_atlas)
            {
                ++in_atlas;
            }
        }

        buffer.clear();
        fmt::format_to(std::back_inserter(buffer), "Scene views: {:08b}", scene_views);
        buffer.push_back('\0');
        mu_text(ctx, buffer.data());

        buffer.clear();
        fmt::format_to(std::back_inserter(buffer), "Raster groups: {:08b}", raster_groups);
        buffer.push_back('\0');
        mu_text(ctx, buffer.data());

        buffer.clear();
        fmt::format_to(std::back_inserter(buffer), "Tiles: {}", _tiles.size());
        buffer.push_back('\0');
        mu_text(ctx, buffer.data());

        buffer.clear();
        fmt::format_to(std::back_inserter(buffer), "Tiles in atlas: {}", in_atlas);
        buffer.push_back('\0');
        mu_text(ctx, buffer.data());

        if (_bounds_tracker.has_value() && mu_begin_treenode(ctx, "Tile bounds"))
        {
            {
                static int layout[] = {24, -1};
                mu_layout_row(ctx, 2, layout, 0);

                mu_text(ctx, "Min:");
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", _bounds_tracker->min_value));
                mu_text(ctx, "Max:");
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", _bounds_tracker->max_value));
            }

            {
                static int layout[] = {-1};
                mu_layout_row(ctx, 1, layout, 0);

                mu_text(
                    ctx,
                    hrz::format_to_buffer(
                        buffer, "{} tiles registered", _bounds_tracker->values.size()));
            }

            std::vector<hrz::TileCoords> sorted_coords;
            sorted_coords.reserve(_bounds_tracker->values.size());

            for (const auto& pair : _bounds_tracker->values)
            {
                sorted_coords.push_back(pair.first);
            }

            std::ranges::sort(
                sorted_coords,
                [](const hrz::TileCoords& a, const hrz::TileCoords& b)
                {
                    if (a.lod == b.lod)
                    {
                        if (a.x == b.x)
                        {
                            return a.y < b.y;
                        }
                        return a.x < b.x;
                    }
                    return a.lod < b.lod;
                });

            static int layout[] = {140, 75, -1};
            mu_layout_row(ctx, 3, layout, 0);

            mu_text(ctx, "Coords");
            mu_text(ctx, "Min");
            mu_text(ctx, "Max");

            for (auto coords : sorted_coords)
            {
                const auto& pair = _bounds_tracker->values.at(coords);
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", coords));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{:.2f}", pair.first));
                mu_text(ctx, hrz::format_to_buffer(buffer, "{:.2f}", pair.second));
            }

            mu_end_treenode(ctx);
        }

        mu_text(ctx, _rasters.empty() ? "No providers" : "Providers");

        for (const auto& raster_pair : _rasters)
        {
            auto* raster = collection->get_raster_by_index(raster_pair.index);
            raster->provider->dev_ui(ctx);
        }
    }
}

} // namespace hrz::planet
