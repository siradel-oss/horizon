#pragma once

#include "hrz/common/geo.h"
#include "hrz/common/tile_coords.h"
#include "hrz/common/vector_data/feature_ids.h"
#include "hrz/common/vector_data/tile_geometry.h"
#include "hrz/core/attribution.h"
#include "hrz/core/channel.h"
#include "hrz/core/scene_model.h"
#include "hrz/protocol/client_data/vector_data_invalidation.pb.h"
#include "hrz/protocol/client_data/vector_data_request_response.pb.h"

#include <cstdint>
#include <variant>

namespace hrz
{
struct AssetsLoader;
struct BlobAllocator;
struct InMemoryVectorDataBase;
struct JobScheduler;
struct ClientMessageQueue;
struct SceneModel;
struct VectorDataLoader;

namespace scene_model
{
class VectorDataLayerPath;
} // namespace scene_model

namespace vector_data
{
enum class DataKind
{
    Geometry,
    AttributeValues,
    FeatureIds
};

/**
 * Create a vector data loader instance.
 */
VectorDataLoader* create_loader(AssetsLoader*, InMemoryVectorDataBase*);

/**
 * Destroys a vector data loader instance.
 */
void destroy_loader(VectorDataLoader*, JobScheduler*);

/**
 * Register a new vector data layer.
 */
void register_layer(VectorDataLoader*, SceneModel*, uint64_t layer_id);

/**
 * Unregister a vector data layer.
 */
void unregister_layer(VectorDataLoader*, uint64_t layer_id);

/**
 * Notify a model change for a vector data layer.
 * Ongoing data requests are not invalidated, though
 * they may be restarted internally.
 */
void notify_update(
    VectorDataLoader*,
    uint64_t layer_id,
    scene_model::UpdateType,
    const scene_model::VectorDataLayerPath&);

/**
 * Advance the requests.
 */
void work(
    VectorDataLoader*,
    SceneModel*,
    JobScheduler*,
    BlobAllocator*,
    ClientMessageQueue*,
    AttributionRegistry*);

namespace messages
{
struct LoadLayer
{
    uint64_t request_id;
    uint32_t layer_id;
};

struct ReleaseLayerLoader
{
    uint64_t request_id;
};

struct RequestData
{
    uint64_t request_id;
    uint64_t load_layer_request_id;
    std::variant<TileCoords, vector_data::FeatureIds> feature_selection;
    DataKind data_kind;
};

struct ReleaseData
{
    uint64_t request_id;
};

struct RetainData
{
    uint64_t request_id;
};

struct ReleaseDataRequest
{
    uint64_t request_id;
};
} // namespace messages

using ToLoaderMessage = std::variant<
    /**
     * Loads the given vector data layer.
     * A vector data layer may not be usable immediately
     * from the information contained in its model. For
     * example some descriptor files may need to be loaded.
     * Creating a layer loader is how users of the vector
     * data loader notify the system that they want to load
     * and setup a layer, in order to be able to request
     * data from it later on.
     * Any model property (such as bounds) and data request
     * must be made through a layer loaded with the loaded
     * status.
     * The loader also allows being told when the model
     * for a vector data layer changes, and if necessary,
     * that new data requests must be made.
     * If only the geometry or attribute data have changed,
     * a change in the model will not be signaled, as these
     * kinds of data updates can be handled by the existing
     * data requests.
     */
    messages::LoadLayer,

    /**
     * Cancels a load layer request.
     */
    messages::ReleaseLayerLoader,

    /**
     * Start a data request for the given vector data layer and
     * data type, selecting features from a tile or a list of IDs.
     *
     * All data that is subsequently retrieved from this request,
     * whether it is geometry, attribute values, or features IDs,
     * is in the same order: the one in which they are defined
     * in the geometry source (when requesting by tile coords) or
     * that of the features ID (when requesting by feature IDs).
     * There can be duplicate feature IDs, either from the source
     * tile or from the feature ID list (in which case duplicate
     * feature IDs are respected and result in duplicate geometry
     * and attribute values).
     */
    messages::RequestData,

    /**
     * Release the data of a request.
     * This does not release the request itself and its data can
     * be obtained again without recreating it.
     * To gain acces to the data again, a `RETAIN_DATA` message
     * must be sent.
     */
    messages::ReleaseData,

    /**
     * Retain the data of a request.
     * If the data of a request has been released, it gets reloaded.
     */
    messages::RetainData,

    /**
     * Release a request, or cancel the request if it has not
     * completed yet.
     */
    messages::ReleaseDataRequest,

    /**
     * Insert vector data given by the client in the loader.
     */
    hrz_proto::VectorDataRequestResponse,

    /**
     * Discard vector data that has been provided by the client.
     * This triggers the creation of new requests to the client
     * for these values, if they are still needed.
     */
    hrz_proto::VectorDataInvalidation>;

namespace messages
{
struct LayerModelUpdate
{
    uint64_t request_id;
    uint32_t min_lod;
    uint32_t max_lod;
    GeoBounds bounds;
    hrz::InlinedVector<uint32_t, 16> attribute_ids;
};

struct LayerModelError
{
    uint64_t request_id;
};

struct LayerNewData
{
    uint64_t request_id;
};

struct DataUpdate
{
    uint64_t request_id{};
    std::variant<VectorTileGeometry, hrz::InlinedVector<AttributeValues, 16>, FeatureIds> data;
    AttributionHandle attribution;
};

struct DataError
{
    uint64_t request_id;
};
} // namespace messages

using FromLoaderMessage = std::variant<
    messages::LayerModelUpdate,
    messages::LayerModelError,
    messages::LayerNewData,
    messages::DataUpdate,
    messages::DataError>;

using VectorDataLoaderChannel = Channel<ToLoaderMessage, FromLoaderMessage>;

/**
 * Create a channel to communicate with the vector data loader.
 */
VectorDataLoaderChannel create_channel(VectorDataLoader*);
} // namespace vector_data
} // namespace hrz
