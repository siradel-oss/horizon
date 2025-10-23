#pragma once

#include "hrz_core_channel.h"
#include "hrz_core_channel_group.h"
#include "planet/hrz_core_planet_raster_merge_group.h"
#include "planet/hrz_core_planet_raster_provider.h"
#include "planet/hrz_core_planet_surface.h"

#include <hrz_common_blob_array.h>
#include <hrz_common_tile_coords.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_variant.h>

#include <deque>
#include <optional>

namespace hrz
{
struct BlobAllocator;

namespace planet
{
struct Raster;
class IRasterCollection;

namespace elevation_query::messages
{
struct QueryElevation
{
    uint64_t query_id;
    hrz::BlobArrayView<lm::dvec2> points;
    monitoring::ResourceOwner resource_owner;
};

struct CancelElevationQuery
{
    uint64_t query_id;
};
} // namespace elevation_query::messages

using ToElevationQueryMessage = std::variant<
    elevation_query::messages::QueryElevation,
    elevation_query::messages::CancelElevationQuery>;

namespace elevation_query::messages
{
struct ElevationQueryResult
{
    uint64_t query_id;
    std::optional<hrz::BlobArray<float>> elevations;
};
}; // namespace elevation_query::messages

using FromElevationQueryMessage = std::variant<elevation_query::messages::ElevationQueryResult>;

class ElevationQuery
{
public:
    using Ticket = ElevationQueryTicket;

private:
    struct ElevationQueryId
    {
        uint64_t channel_id;
        uint64_t query_id;

        constexpr bool operator==(const ElevationQueryId& other) const = default;

        template<typename H>
        friend H AbslHashValue(H h, const ElevationQueryId& id)
        {
            return H::combine(std::move(h), id.channel_id, id.query_id);
        }
    };

    struct Batch
    {
        enum Status
        {
            Queued,
            Culling,
            WaitingForTiles,
            ReadyToSample,
            Sampling,
            Finished,
        };

        struct RasterTile
        {
            uint32_t raster_index;
            uint64_t raster_id;
            TileCoords tile;
            RasterProvider::LockTicket lock_ticket;
        };

        std::optional<ElevationQueryId> query_id;
        Status status = Queued;
        size_t point_count;
        monitoring::ResourceOwner resource_owner;
        ElevationQueryPointStorage points;
        std::optional<hrz::BlobArray<float>> elevations;
        std::vector<RasterTile> tiles;

        size_t raster_count;
        uint64_t raster_ids_hash;

        hrz_jobs::CullPointsQueryTicket cull_ticket;
        hrz_jobs::SamplePointsQueryTicket sample_ticket;

        std::optional<hrz::BlobArrayAllocation<double>> elevations_allocation;
    };

    // We use 31 as generation count so we can reserve the ~0 value for
    // indicating no ticket.
    using BatchPool = GenObjectPool<Batch, GenIndexPool<Ticket, 31, 32>, 32>;

    static const Ticket NoTicket = ~0ull;

    enum
    {
        MaxCulling = 64,
        MaxSampling = 64,
    };

    uint64_t _raster_group_revision = ~0ull;
    BatchPool _batchs;
    std::deque<Ticket> _queued;
    std::vector<Ticket> _culling; // There can be MaxCulling jobs at once at this step.
    std::vector<Ticket> _waiting_for_tiles;
    std::deque<Ticket> _ready_to_sample;
    std::vector<Ticket> _sampling; // There can be only MaxSampling jobs at once at this step.
    std::vector<Ticket> _finished;

    hrz::flat_hash_map<ElevationQueryId, Ticket> _query_ids_to_tickets;

    hrz::flat_hash_set<Ticket> _to_cancel;

    ChannelGroup<FromElevationQueryMessage, ToElevationQueryMessage> _channels;

public:
    ElevationQuery() = default;

    void queue_cancel(Ticket ticket);

    // Positions must be in Web Mercator (EPSG:3857).
    Ticket start_query(hrz::BlobArray<lm::dvec2> points, monitoring::ResourceOwner resource_owner);
    Ticket start_query(
        hrz::BlobArrayView<lm::dvec2> points,
        monitoring::ResourceOwner resource_owner);
    Ticket start_query(const lm::dvec2& point, monitoring::ResourceOwner resource_owner);

    bool is_ready(Ticket ticket) const;

    // Returns an empty optional when no DTM rasters are defined in the scene.
    // In this case, all elevations can considered to be 0.
    std::optional<hrz::BlobArray<float>> retrieve(Ticket ticket);

    void work(
        JobScheduler* js,
        AssetsLoader* al,
        BlobAllocator* ba,
        IRasterCollection* collection,
        RasterMergeGroup* rg);

    void dev_ui(mu_Context* ctx, const char* window_name);

private:
    Ticket start_query(
        hrz::BlobArrayView<lm::dvec2> points,
        monitoring::ResourceOwner resource_owner,
        std::optional<ElevationQueryId> query_id);

    template<typename T>
    void _restart_batchs(
        JobScheduler* js,
        BlobAllocator* ba,
        IRasterCollection* collection,
        T& container)
    {
        // We always set `remove_from_collection` to false because we will
        // clear right after, which is more efficient that using .erase on
        // vectors and deques.
        for (Ticket ticket : container)
        {
            _cancel(js, ba, ticket, collection, false);
            Batch* batch = _batchs.get_object(ticket);
            assert(batch);
            batch->status = Batch::Queued;
            _queued.push_back(ticket);
        }
        container.clear();
    }

    void _restart_all_batchs(JobScheduler* js, BlobAllocator* ba, IRasterCollection* collection);

    // Cancels a batch. This does not release it from the pool.
    // The batch is left unassigned to any status.
    void _cancel(
        JobScheduler* js,
        BlobAllocator* ba,
        Ticket ticket,
        IRasterCollection* collection,
        bool remove_from_collection = true);

    void _start_culling(
        JobScheduler* js,
        uint64_t raster_ids_hash,
        std::span<const Raster*> rasters,
        std::span<const hrz::planet::TiledRasterGeometry> raster_geometries,
        Ticket ticket);

    void _start_waiting_for_tiles(
        JobScheduler* js,
        AssetsLoader* al,
        Ticket ticket,
        std::span<const Raster*> rasters);

    bool _all_tiles_available(const Batch* batch, std::span<const Raster*> rasters);

    void _start_sampling(
        JobScheduler* js,
        BlobAllocator* ba,
        std::span<const Raster*> rasters,
        std::span<const hrz::planet::TiledRasterGeometry> raster_geometries,
        Ticket ticket,
        Batch* batch);

    void _handle_finished_batch(Ticket ticket, Batch* batch);

public:
    using Channel = hrz::Channel<ToElevationQueryMessage, FromElevationQueryMessage>;

    Channel create_channel();
};
} // namespace planet
} // namespace hrz
