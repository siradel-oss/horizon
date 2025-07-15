#include "vector/data_loader/hrz_core_vector_data_loader.h"

#include "assets_loader/hrz_core_assets_loader.h"
#include "hrz_core_client_messages.h"
#include "vector/data_loader/hrz_core_vector_data_loader_impl.h"

extern "C"
{
#include <microui/microui.h>
}

namespace hrz
{
namespace in_memory = vector_data::in_memory;

namespace vector_data
{
VectorDataLoader* create_loader(AssetsLoader* al, InMemoryVectorDataBase* in_memory_database)
{
    assert(al && in_memory_database);

    return VectorDataLoader::create(al, in_memory_database);
}

void destroy_loader(VectorDataLoader* loader, JobScheduler* js)
{
    assert(loader);
    loader->destroy(js);
    delete loader;
}

void register_layer(VectorDataLoader* loader, SceneModel* scene_model, uint64_t layer_id)
{
    assert(loader);
    loader->register_layer(scene_model, layer_id);
}

void unregister_layer(VectorDataLoader* loader, uint64_t layer_id)
{
    assert(loader);
    loader->unregister_layer(layer_id);
}

void notify_update(
    VectorDataLoader* loader,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::VectorDataLayerPath& path)
{
    assert(loader);
    loader->notify_update(layer_id, update_type, path);
}

void work(
    VectorDataLoader* loader,
    SceneModel* scene_model,
    JobScheduler* js,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    AttributionRegistry* attributions)
{
    assert(loader);
    loader->work(scene_model, js, ba, mq, attributions);
}

VectorDataLoaderChannel create_channel(VectorDataLoader* loader)
{
    assert(loader);
    return loader->create_channel();
}

void dev_ui(VectorDataLoader* loader, mu_Context* ctx, const char* window_name)
{
    if (mu_begin_window_ex(ctx, window_name, mu_rect(300, 100, 600, 500), MU_OPT_CLOSED))
    {
        loader->dev_ui(ctx);
        mu_end_window(ctx);
    }
}
} // namespace vector_data
} // namespace hrz
