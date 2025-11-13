#include "hrz/core/scene_model.h"

#include "hrz/common/profiling.h"
#include "hrz/core/scene_model_accessor.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/thread.h"
#include "hrz/fnd/time.h"

extern "C"
{
#include <microui/microui.h>
}

#include <mutex>

static constexpr double DEFRAG_DELAY_MS = 5000;

namespace pb
{
using google::protobuf::Arena;
}

namespace
{
template<typename T>
struct ArenaMessage
{
    std::unique_ptr<pb::Arena> arena;
    T* msg;
    uint64_t previous_used_bytes;

    void init()
    {
        arena.reset(new pb::Arena());
        msg = pb::Arena::Create<T>(arena.get());
        previous_used_bytes = arena->SpaceUsed();
    }

    void defrag()
    {
        uint64_t current_used_bytes = arena->SpaceUsed();
        if (current_used_bytes >= 4 * previous_used_bytes)
        {
            std::unique_ptr<pb::Arena> new_arena(new pb::Arena());
            T* new_msg = pb::Arena::Create<T>(new_arena.get());

            new_msg->MergeFrom(*msg);

            msg = new_msg;
            arena.swap(new_arena);
            previous_used_bytes = arena->SpaceUsed();
        }
    }
};

std::span<const uint32_t> _path_to_span(const hrz_proto::Path& path)
{
    auto& field = path.parts();
    return std::span<const uint32_t>(field.data(), field.size());
}

const char* path_root_name(hrz_proto::PathRoot::KindCase kind)
{
    switch (kind)
    {
        case hrz_proto::PathRoot::kSingleModelLayer: return "single model layer";
        case hrz_proto::PathRoot::kDtmRasterLayer: return "DTM raster layer";
        case hrz_proto::PathRoot::kImageryRasterLayer: return "imagery raster layer";
        case hrz_proto::PathRoot::kVectorDataLayer: return "vector data layer";
        case hrz_proto::PathRoot::kVectorTilesLayer: return "vector tile layer";
        case hrz_proto::PathRoot::kThreeDTilesLayer: return "3D Tiles layer";
        case hrz_proto::PathRoot::kClippingPlaneLayer: return "clipping plane layer";
        case hrz_proto::PathRoot::kInMemoryVectorSourceLayer:
            return "in-memory vector source layer";
        case hrz_proto::PathRoot::kGizmoLayer: return "gizmo layer";
        case hrz_proto::PathRoot::kSceneSettings: return "scene settings";
        case hrz_proto::PathRoot::kSceneViewSettings: return "scene view settings";
        case hrz_proto::PathRoot::kCameraSettings: return "camera settings";
        default: return "unknown";
    }
}

const char* path_root_name(const hrz_proto::PathRoot& root_type)
{
    return path_root_name(root_type.kind_case());
}

#define LOG_INVALID_ROOT(op, root_type) \
    HRZ_LOG_ERROR("[{}] Invalid {} path root", op, path_root_name(root_type));

class SceneModelRootType
{
public:
    virtual ~SceneModelRootType() = default;

    virtual size_t size() const = 0;

    virtual void register_element(const hrz_proto::PathRoot& root) = 0;
    virtual void unregister_element(const hrz_proto::PathRoot& root) = 0;

    virtual std::string get_raw(const hrz_proto::Path& path) const = 0;
    virtual uint32_t count(const hrz_proto::Path& path) const = 0;

    virtual void set_raw(const hrz_proto::Path& path, std::string_view raw) = 0;
    virtual uint32_t add_raw(const hrz_proto::Path& path, std::string_view raw) = 0;
    virtual uint32_t remove(const hrz_proto::Path& path) = 0;

    virtual void defrag() = 0;
};

template<typename ModelType>
class SingleSceneModelRootType : public SceneModelRootType
{
    ArenaMessage<ModelType> _model;
    mutable std::shared_mutex _mutex;

public:
    SingleSceneModelRootType() { _model.init(); }

    size_t size() const final { return 1; }

    void register_element(const hrz_proto::PathRoot& root) final
    {
        HRZ_LOG_WARNING("Cannot register more than one {} root.", path_root_name(root));
    }

    void unregister_element(const hrz_proto::PathRoot& root) final
    {
        HRZ_LOG_WARNING("Cannot unregister a {} root.", path_root_name(root));
    }

    std::string get_raw(const hrz_proto::Path& path) const final
    {
        HRZ_SCOPED_SHARED_LOCK(_mutex);
        return hrz::scene_model::get_message_part_raw(*_model.msg, _path_to_span(path));
    }

    uint32_t count(const hrz_proto::Path& path) const final
    {
        HRZ_SCOPED_SHARED_LOCK(_mutex);
        return hrz::scene_model::count_message_part(*_model.msg, _path_to_span(path));
    }

    void set_raw(const hrz_proto::Path& path, std::string_view raw) final
    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(_mutex);
        hrz::scene_model::set_message_part_raw(*_model.msg, _path_to_span(path), raw);
    }

    uint32_t add_raw(const hrz_proto::Path& path, std::string_view raw) final
    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(_mutex);
        return hrz::scene_model::add_message_part_raw(*_model.msg, _path_to_span(path), raw);
    }

    uint32_t remove(const hrz_proto::Path& path) final
    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(_mutex);
        return hrz::scene_model::remove_message_part(*_model.msg, _path_to_span(path));
    }

    void defrag() final
    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(_mutex);
        _model.defrag();
    }
};

template<typename KeyType, typename ModelType>
class MultiSceneModelRootType : public SceneModelRootType
{
    hrz::flat_hash_map<KeyType, ArenaMessage<ModelType>> _models;
    std::function<KeyType(const hrz_proto::PathRoot&)> _get_key;
    mutable std::shared_mutex _mutex;

public:
    explicit MultiSceneModelRootType(
        const std::function<KeyType(const hrz_proto::PathRoot&)>& get_key_fn) :
        _get_key{get_key_fn}
    {
    }

    size_t size() const final
    {
        HRZ_SCOPED_SHARED_LOCK(_mutex);
        return _models.size();
    }

    void register_element(const hrz_proto::PathRoot& root) final
    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(_mutex);

        KeyType key = _get_key(root);
        auto it = _models.find(key);
        if (it == _models.end())
        {
            _models[key].init();
        }
        else
        {
            LOG_INVALID_ROOT("register_element", root);
        }
    }

    void unregister_element(const hrz_proto::PathRoot& root) final
    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(_mutex);

        KeyType key = _get_key(root);
        auto it = _models.find(key);
        if (it != _models.end())
        {
            _models.erase(it);
        }
        else
        {
            LOG_INVALID_ROOT("unregister_element", root);
        }
    }

    std::string get_raw(const hrz_proto::Path& path) const final
    {
        HRZ_SCOPED_SHARED_LOCK(_mutex);

        KeyType key = _get_key(path.root());
        auto it = _models.find(key);
        if (it != _models.end())
        {
            return hrz::scene_model::get_message_part_raw(*it->second.msg, _path_to_span(path));
        }
        else
        {
            LOG_INVALID_ROOT("get_raw", path.root());
            return {};
        }
    }

    uint32_t count(const hrz_proto::Path& path) const final
    {
        HRZ_SCOPED_SHARED_LOCK(_mutex);

        KeyType key = _get_key(path.root());
        auto it = _models.find(key);
        if (it != _models.end())
        {
            return hrz::scene_model::count_message_part(*it->second.msg, _path_to_span(path));
        }
        else
        {
            LOG_INVALID_ROOT("count", path.root());
            return 0;
        }
    }

    void set_raw(const hrz_proto::Path& path, std::string_view raw) final
    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(_mutex);

        KeyType key = _get_key(path.root());
        auto it = _models.find(key);
        if (it != _models.end())
        {
            hrz::scene_model::set_message_part_raw(*it->second.msg, _path_to_span(path), raw);
        }
        else
        {
            LOG_INVALID_ROOT("set_raw", path.root());
        }
    }

    uint32_t add_raw(const hrz_proto::Path& path, std::string_view raw) final
    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(_mutex);

        KeyType key = _get_key(path.root());
        auto it = _models.find(key);
        if (it != _models.end())
        {
            return hrz::scene_model::add_message_part_raw(
                *it->second.msg, _path_to_span(path), raw);
        }
        else
        {
            LOG_INVALID_ROOT("add_raw", path.root());
            return 0;
        }
    }

    uint32_t remove(const hrz_proto::Path& path) final
    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(_mutex);

        KeyType key = _get_key(path.root());
        auto it = _models.find(key);
        if (it != _models.end())
        {
            return hrz::scene_model::remove_message_part(*it->second.msg, _path_to_span(path));
        }
        else
        {
            LOG_INVALID_ROOT("remove", path.root());
            return 0;
        }
    }

    void defrag() final
    {
        HRZ_SCOPED_EXCLUSIVE_LOCK(_mutex);

        for (auto& model : _models)
        {
            model.second.defrag();
        }
    }
};

template<typename LayerModelType>
class LayerSceneModelRootType : public MultiSceneModelRootType<uint64_t, LayerModelType>
{
public:
    explicit LayerSceneModelRootType(
        const std::function<uint64_t(const hrz_proto::PathRoot&)>& get_key_fn) :
        MultiSceneModelRootType<uint64_t, LayerModelType>(get_key_fn)
    {
    }
};

} // namespace

namespace hrz
{
struct SceneModel
{
    hrz::flat_hash_map<hrz_proto::PathRoot::KindCase, std::unique_ptr<SceneModelRootType>> models;
    double last_defrag_ms;
};

namespace scene_model
{
SceneModel* create()
{
    SceneModel* model = new SceneModel();
    model->last_defrag_ms = hrz::now_frame_ms();

    model->models.insert(std::make_pair(
        hrz_proto::PathRoot::kSingleModelLayer,
        std::make_unique<LayerSceneModelRootType<hrz_proto::SingleModelLayer>>(
            [](const hrz_proto::PathRoot& root) { return root.single_model_layer().opaque(); })));

    model->models.insert(std::make_pair(
        hrz_proto::PathRoot::kDtmRasterLayer,
        std::make_unique<LayerSceneModelRootType<hrz_proto::DtmRasterLayer>>(
            [](const hrz_proto::PathRoot& root) { return root.dtm_raster_layer().opaque(); })));

    model->models.insert(std::make_pair(
        hrz_proto::PathRoot::kImageryRasterLayer,
        std::make_unique<LayerSceneModelRootType<hrz_proto::ImageryRasterLayer>>(
            [](const hrz_proto::PathRoot& root) { return root.imagery_raster_layer().opaque(); })));

    model->models.insert(std::make_pair(
        hrz_proto::PathRoot::kVectorDataLayer,
        std::make_unique<LayerSceneModelRootType<hrz_proto::VectorDataLayer>>(
            [](const hrz_proto::PathRoot& root) { return root.vector_data_layer().opaque(); })));

    model->models.insert(std::make_pair(
        hrz_proto::PathRoot::kVectorTilesLayer,
        std::make_unique<LayerSceneModelRootType<hrz_proto::VectorTilesLayer>>(
            [](const hrz_proto::PathRoot& root) { return root.vector_tiles_layer().opaque(); })));

    model->models.insert(std::make_pair(
        hrz_proto::PathRoot::kThreeDTilesLayer,
        std::make_unique<LayerSceneModelRootType<hrz_proto::ThreeDTilesLayer>>(
            [](const hrz_proto::PathRoot& root) { return root.three_d_tiles_layer().opaque(); })));

    model->models.insert(std::make_pair(
        hrz_proto::PathRoot::kClippingPlaneLayer,
        std::make_unique<LayerSceneModelRootType<hrz_proto::ClippingPlaneLayer>>(
            [](const hrz_proto::PathRoot& root) { return root.clipping_plane_layer().opaque(); })));

    model->models.insert(std::make_pair(
        hrz_proto::PathRoot::kInMemoryVectorSourceLayer,
        std::make_unique<LayerSceneModelRootType<hrz_proto::InMemoryVectorSourceLayer>>(
            [](const hrz_proto::PathRoot& root)
            { return root.in_memory_vector_source_layer().opaque(); })));

    model->models.insert(std::make_pair(
        hrz_proto::PathRoot::kGizmoLayer,
        std::make_unique<LayerSceneModelRootType<hrz_proto::GizmoLayer>>(
            [](const hrz_proto::PathRoot& root) { return root.gizmo_layer().opaque(); })));

    model->models.insert(std::make_pair(
        hrz_proto::PathRoot::kEditableShapeLayer,
        std::make_unique<LayerSceneModelRootType<hrz_proto::EditableShapeLayer>>(
            [](const hrz_proto::PathRoot& root) { return root.editable_shape_layer().opaque(); })));

    model->models.insert(std::make_pair(
        hrz_proto::PathRoot::kSceneViewSettings,
        std::make_unique<
            MultiSceneModelRootType<hrz_proto::SceneViewIndex, hrz_proto::SceneViewSettings>>(
            [](const hrz_proto::PathRoot& root) { return root.scene_view_settings(); })));

    model->models.insert(std::make_pair(
        hrz_proto::PathRoot::kCameraSettings,
        std::make_unique<
            MultiSceneModelRootType<hrz_proto::CameraIndex, hrz_proto::CameraSettings>>(
            [](const hrz_proto::PathRoot& root) { return root.camera_settings(); })));

    model->models.insert(std::make_pair(
        hrz_proto::PathRoot::kSceneSettings,
        std::make_unique<SingleSceneModelRootType<hrz_proto::SceneSettings>>()));

    return model;
}

void destroy(SceneModel* model)
{
    delete model;
}

void defrag(SceneModel* model)
{
    HRZ_SCOPED_SAMPLE("scene model defrag");

    assert(model);
    double now = hrz::now_frame_ms();

    if (now - model->last_defrag_ms >= DEFRAG_DELAY_MS)
    {
        for (auto& sys : model->models)
        {
            sys.second->defrag();
        }
        model->last_defrag_ms = now;
    }
}

void register_element(SceneModel* model, const hrz_proto::PathRoot& root)
{
    assert(model);
    auto it = model->models.find(root.kind_case());
    assert(
        it != model->models.end()
        && "Usage of consumed path, or outdated client, or unimplemented case");
    if (it != model->models.end())
    {
        it->second->register_element(root);
    }
}

void unregister_element(SceneModel* model, const hrz_proto::PathRoot& root)
{
    assert(model);
    auto it = model->models.find(root.kind_case());
    assert(
        it != model->models.end()
        && "Usage of consumed path, or outdated client, or unimplemented case");
    if (it != model->models.end())
    {
        it->second->unregister_element(root);
    }
}

std::string get_raw(const SceneModel* model, const hrz_proto::Path& path)
{
    assert(model);
    auto it = model->models.find(path.root().kind_case());
    assert(
        it != model->models.end()
        && "Usage of consumed path, or outdated client, or unimplemented case");
    if (it != model->models.end())
    {
        return it->second->get_raw(path);
    }
    return std::string();
}

void set_raw(SceneModel* model, const hrz_proto::Path& path, std::string_view raw)
{
    assert(model);
    auto it = model->models.find(path.root().kind_case());
    assert(
        it != model->models.end()
        && "Usage of consumed path, or outdated client, or unimplemented case");
    if (it != model->models.end())
    {
        it->second->set_raw(path, raw);
    }
}

uint32_t count(const SceneModel* model, const hrz_proto::Path& path)
{
    assert(model);
    auto it = model->models.find(path.root().kind_case());
    assert(
        it != model->models.end()
        && "Usage of consumed path, or outdated client, or unimplemented case");
    if (it != model->models.end())
    {
        return it->second->count(path);
    }
    return 0;
}

uint32_t add_raw(SceneModel* model, const hrz_proto::Path& path, std::string_view raw)
{
    assert(model);
    auto it = model->models.find(path.root().kind_case());
    assert(
        it != model->models.end()
        && "Usage of consumed path, or outdated client, or unimplemented case");
    if (it != model->models.end())
    {
        return it->second->add_raw(path, raw);
    }
    return 0;
}

uint32_t remove(SceneModel* model, const hrz_proto::Path& path)
{
    assert(model);
    auto it = model->models.find(path.root().kind_case());
    assert(
        it != model->models.end()
        && "Usage of consumed path, or outdated client, or unimplemented case");
    if (it != model->models.end())
    {
        return it->second->remove(path);
    }
    return 0;
}

void dev_ui(const SceneModel* model, mu_Context* ctx)
{
    int layout_full[] = {-1};

    mu_layout_row(ctx, 1, layout_full, 0);
    mu_text(ctx, "Number of elements per root type");

    int layout_columns[] = {200, -1};

    mu_layout_row(ctx, 2, layout_columns, 0);

    static fmt::memory_buffer buffer;
    for (const auto& [root_type, model_root] : model->models)
    {
        mu_text(ctx, path_root_name(root_type));

        buffer.clear();
        fmt::format_to(std::back_inserter(buffer), "{}", model_root->size());
        buffer.push_back('\0');
        mu_label(ctx, buffer.data());
    }
}

} // namespace scene_model
} // namespace hrz
