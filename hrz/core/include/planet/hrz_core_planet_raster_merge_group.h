#pragma once

#include "hrz_core_visibility_constraints.h"
#include "hrz_jobs_tickets.h"
#include "planet/hrz_core_planet_raster_data_fetch_types.h"
#include "planet/hrz_core_planet_raster_provider.h"
#include "vtex/hrz_core_vtex_clipmap_params.h"
#include "vtex/hrz_core_vtex_page_cache_manager.h"

#include <hrz_common_blob_image.h>
#include <hrz_common_geo.h>
#include <hrz_common_planet.h>
#include <hrz_common_tile_coords.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_node_hash_map.h>
#include <hrz_fnd_unique_vector.h>
#include <hrz_jobs_protocol.h>
#include <hrz_protocol_all.h>

#include <gsl/gsl-lite.hpp>

#include <cstdint>
#include <optional>
#include <vector>

extern "C"
{
#include <microui/microui.h>
}

namespace hrz
{
struct AssetsLoader;
struct BlobAllocator;
struct JobScheduler;

namespace planet
{
struct Raster;
class IRasterCollection;

struct CollectionRasterReference
{
    // Index of the raster in the collection, not stable but fast lookup.
    size_t index;

    // ID of the raster. Stable but slower lookup.
    uint64_t id;

    uint64_t unique_id;
};

using PixelToValueFunction = double (*)(const void* pixel);

struct TileBoundsTracker
{
    PixelToValueFunction pixel_to_value;
    hrz::flat_hash_map<hrz::TileCoords, std::pair<double, double>> values;
    uint64_t version;
};

// A `RasterMergeGroup` instance represents a set of rasters
// that are drawable with a single virtual texture on the
// planet geometry. It maintains a page table and
// and an indirection texture to this effect.
class RasterMergeGroup
{
    struct HashList
    {
        static constexpr size_t CAPACITY = 4;
        size_t hashes[CAPACITY];
        size_t hash_count = 0;
        size_t next_hash_index = 0;

        void clear()
        {
            hash_count = 0;
            next_hash_index = 0;
        }

        bool contains(size_t hash)
        {
            for (size_t i = 0; i < hash_count; ++i)
            {
                if (hashes[i] == hash) return true;
            }

            return false;
        }

        void push(size_t hash)
        {
            hashes[next_hash_index] = hash;
            if (hash_count < CAPACITY) hash_count += 1;
            next_hash_index = (next_hash_index + 1) % CAPACITY;
        }
    };

    struct ComposedTile
    {
        enum class Status
        {
            WaitingForProvider,
            Reprojecting,
            Reprojected,
            LoadingTiles,
            Loaded,
            Composing,
            Compressing,
            Composed,
            Displayed,
            Outdated,
            Evicted,
            Error,
        };

        struct ReprojectedTile
        {
            TileCoords coords;

            struct Mesh
            {
                lm::Vector<uint16_t, 2> grid_size;
                std::vector<lm::vec2> grid;
            };

            std::optional<Mesh> mesh;
            lm::bbox2 uv_clip;

            bool was_visible = false;
            RasterProvider::LockTicket lock_ticket;
        };

        struct TileRaster
        {
            enum class Status
            {
                WaitingForProvider,
                Reprojecting,
                Reprojected,
                LoadingTiles,
                Loaded,
            };

            uint64_t raster_id;
            std::vector<ReprojectedTile> reprojected_tiles;
            hrz_jobs::ReprojectRasterTileTicket reproject_tile_ticket;
            Status status;
            bool fully_composed = false;

            void swap(TileRaster& p)
            {
                if (this != &p)
                {
                    std::swap(p.status, status);
                    std::swap(p.raster_id, raster_id);
                    std::swap(p.reproject_tile_ticket, reproject_tile_ticket);
                    std::swap(p.reprojected_tiles, reprojected_tiles);
                    std::swap(p.fully_composed, fully_composed);
                }
            }
        };

        Status status;
        bool in_atlas;
        TileCoords coords;
        int uses;
        hrz_jobs::ComposeRasterTileTicket compose_tile_ticket;
        hrz_jobs::CompressBlobImageTicket compress_tile_ticket;
        BlobImage image;
        std::vector<TileRaster> rasters;

        hrz::InlinedUniqueVector<AttributionHandle, 8> unique_attributions;
    };

    struct FetchRequest
    {
        RasterProvider::SourceLockTicket lock;
        TileCoords tile;
        lm::ivec2 pixel_pos;
        lm::dvec3 proj_pos;
        uint64_t raster_id;

        hrz_proto::PickLayerResult result;
    };

    using FetchRequestPool =
        GenObjectPool<FetchRequest, GenIndexPool<RasterDataFetchMergeGroupTicket, 15, 16>, 16>;

    uint32_t _atlas_size;
    uint32_t _num_slots_in_atlas;

    std::unique_ptr<vtex::PageCacheManager> _page_cache;
    gsl::span<const hrz_proto::ImageFormat> _source_image_formats;
    hrz_proto::ImageFormat _composed_image_format;
    my::TextureFormat _atlas_image_format;

    std::string _name;

    // First is index in collection, second is id.
    std::vector<CollectionRasterReference> _rasters;
    uint128 _rasters_hash;
    uint32_t _scene_views_bitset;

    hrz::node_hash_map<TileCoords, ComposedTile> _tiles;
    std::vector<ComposedTile*> _sorted_tiles; // Pointers point to values of _tiles
    uint64_t _revision = 0;

    HashList _requested_tile_list_hashes;

    FetchRequestPool _fetch_request_pool;
    hrz::flat_hash_set<RasterDataFetchMergeGroupTicket> _incomplete_fetch_requests;
    hrz::flat_hash_set<RasterDataFetchMergeGroupTicket> _completed_fetch_requests;

    std::optional<TileBoundsTracker> _bounds_tracker;

public:
    // Create an empty `RasterMergeGroup` instance, using the given page cache.
    // This group does not have any raster and thus won't draw anything
    // in its textures.
    // If a `TileBoundsTracker` struct is given, the min and max values of composed tiles
    // (which are computed for each pixel of the image using the `pixel_to_value` function
    // specified in the struct) will be kept in memory and available to read.
    // The `source_image_formats` span must be static.
    RasterMergeGroup(
        uint32_t atlas_size,
        std::unique_ptr<vtex::PageCacheManager> page_cache,
        gsl::span<const hrz_proto::ImageFormat> source_image_formats,
        hrz_proto::ImageFormat composed_image_format,
        std::optional<TileBoundsTracker>&& bounds_tracker,
        std::string_view name);

    void destroy(AssetsLoader* al, JobScheduler* js, Render* render);

    uint64_t get_revision() const { return _revision; }

    bool is_empty() const { return _rasters.empty(); }

    gsl::span<const CollectionRasterReference> get_raster() const { return _rasters; }

    my::ResourceHandle get_clipmap_texture() const { return _page_cache->get_clipmap_texture(); }

    my::ResourceHandle get_clipmap_sampler() const { return _page_cache->get_clipmap_sampler(); }

    my::ResourceHandle get_table_texture() const { return _page_cache->get_table_texture(); }

    // Recenter the page cache according to the given clipmap params.
    // This method must be called each time the clipmap is moved in
    // order to keep this raster in sync.
    inline void recenter(const vtex::ClipmapParams& clipmap_params)
    {
        _page_cache->recenter(clipmap_params);
    }

    bool upload_composed_tiles_and_bake_clipmap();

    void get_attributions(hrz::InlinedUniqueVector<AttributionHandle, 8>* attributions) const;

    void update_requested_tiles(
        gsl::span<const gsl::span<const TileCoordsWithUsage>> requested_tiles,
        size_t requested_tiles_hash,
        IRasterCollection* collection,
        AssetsLoader* al,
        JobScheduler* js);

    void restart_tiles(IRasterCollection* collection, JobScheduler* js);

    void set_rasters(
        IRasterCollection* collection,
        JobScheduler* js,
        gsl::span<const CollectionRasterReference> rasters,
        uint32_t scene_views_bitset);

    void invalidate_raster_tiles(
        IRasterCollection* collection,
        uint64_t raster_id,
        JobScheduler* js);

    void work(IRasterCollection* collection, AssetsLoader* al, BlobAllocator* ba, JobScheduler* js);

    void work_gpu(Render* render, BlobAllocator* ba);

    void pick(
        IRasterCollection* collection,
        const lm::dvec3& position,
        gsl::span<const hrz_proto::LayerHandle> included_rasters,
        hrz_proto::PickResults&);

    void schedule_raster_data_fetch(
        IRasterCollection* collection,
        const GeoPosition2& position,
        gsl::span<const hrz_proto::LayerHandle> layers,
        std::vector<RasterDataFetchMergeGroupTicket>& out_tickets);

    bool is_data_fetch_ready(RasterDataFetchMergeGroupTicket ticket) const;

    hrz_proto::PickLayerResult get_data_fetch_result(RasterDataFetchMergeGroupTicket ticket);

    void dev_ui(
        IRasterCollection* collection,
        mu_Context* ctx,
        const char* header_title,
        uint32_t scene_views,
        uint32_t raster_groups);

    bool is_working(const IRasterCollection* collection) const;

    bool check_raster_format(const Raster* raster);

    // Only call these methods if the `RasterMergeGroup` have been provided a `TileBoundsTracker`
    // during construction.
    uint64_t get_bounds_tracker_version() const;
    bool get_tile_bounds(const hrz::TileCoords& coords, double* min, double* max) const;

private:
    void cancel_jobs_and_release_tiles(
        IRasterCollection*,
        JobScheduler* js,
        ComposedTile& tile,
        bool nominate_tiles_for_eviction = false);
    void cancel_jobs_and_release_tiles(
        IRasterCollection*,
        JobScheduler* js,
        ComposedTile::TileRaster& tile_raster,
        bool nominate_tiles_for_eviction = false);
    hrz_jobs::ReprojectRasterTileTicket create_reprojection_job(
        TileCoords tile_coords,
        const Raster* raster,
        JobScheduler* js);
    uint128 compute_rasters_hash(gsl::span<const CollectionRasterReference> rasters) const;

    inline bool is_visible_in(const layers::MultiviewVisibilityConstraints& visibility) const
    {
        return visibility.satisfied_in & _scene_views_bitset;
    }
};

} // namespace planet
} // namespace hrz
