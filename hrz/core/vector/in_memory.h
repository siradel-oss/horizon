#pragma once

#include "hrz/common/tile_coords.h"
#include "hrz/common/vector_data/feature_ids.h"
#include "hrz/common/vector_data/tile.h"
#include "hrz/core/attribution.h"
#include "hrz/core/channel.h"
#include "hrz/core/scene_model.h"

#include <cstdint>
#include <variant>

namespace hrz
{
struct BlobAllocator;
struct InMemoryVectorDataBase;

namespace scene_model
{
class InMemoryVectorSourceLayerPath;
}

namespace vector_data
{
struct DecodedVectorTile;
struct FeatureIds;

namespace in_memory
{
/**
 * Create an in-memory vector database instance.
 */
InMemoryVectorDataBase* create_system();

/**
 * Destroy an in-memory vector database instance.
 */
void destroy_system(InMemoryVectorDataBase*, SceneModel*, BlobAllocator*);

/**
 * Register a new in-memory vector source layer.
 */
void register_layer(InMemoryVectorDataBase*, SceneModel*, uint64_t layer_handle);

/**
 * Unregister a new in-memory vector source layer.
 */
void unregister_layer(InMemoryVectorDataBase*, uint64_t layer_handle);

/**
 * Notify a model change for an in-memory vector source layer.
 */
void notify_update(
    InMemoryVectorDataBase*,
    uint64_t layer_handle,
    scene_model::UpdateType update_type,
    const scene_model::InMemoryVectorSourceLayerPath& path);

/**
 * Advance internal work.
 */
void work(InMemoryVectorDataBase*, SceneModel*, BlobAllocator*, AttributionRegistry*);

namespace messages
{
/*
 * Request for the given in-memory vector source layer and tile coordinates.
 * All data that is subsequently obtained from this request, whether
 * it is geometry, attribute values, or features IDs, is in the same
 * order: the one in which they are defined in the model. There can be
 * duplicate feature IDs.
 */
struct TileDataRequest
{
    uint64_t request_id;
    uint32_t in_memory_layer_id;
    TileCoords tile_coords;
    bool subscribe_to_updates;
};

/**
 * Request for the given in-memory vector source layer and features.
 * All data that is subsequently obtained from this request, whether
 * it is geometry, attribute values, or features IDs, is in the same
 * order: that of the parameter `feature_ids`. Duplicate feature IDs
 * are respected and result in duplicate geometry and attribute values.
 */
struct FeatureDataRequest
{
    uint64_t request_id;
    uint32_t in_memory_layer_id;
    hrz::vector_data::FeatureIds feature_ids;
    bool subscribe_to_updates;
};

/**
 * Release a request, or cancel the request if it has not completed yet.
 */
struct ReleaseDataRequest
{
    uint64_t request_id;
};
} // namespace messages

using ToInMemoryMessages = std::
    variant<messages::TileDataRequest, messages::FeatureDataRequest, messages::ReleaseDataRequest>;

namespace messages
{
struct VectorData
{
    uint64_t request_id;
    vector_data::DecodedVectorTile data;
    AttributionHandle attribution;
};

struct Error
{
    uint64_t request_id;
};
} // namespace messages

using FromInMemoryMessages = std::variant<messages::VectorData, messages::Error>;

using InMemoryChannel = Channel<ToInMemoryMessages, FromInMemoryMessages>;

InMemoryChannel create_channel(InMemoryVectorDataBase*);
} // namespace in_memory
} // namespace vector_data
} // namespace hrz
