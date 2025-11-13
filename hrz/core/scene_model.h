#pragma once

#include "hrz/protocol/all.h"

#include <string>
#include <string_view>

struct mu_Context;

namespace hrz
{
/**
 * The scene model contains the entier scene description.
 * It can be accessed through the methods below.
 * Each action requires serialization and deserialization.
 * No interaction is thread-safe.
 */
struct SceneModel;

namespace scene_model
{
enum class UpdateType
{
    Set,
    Add,
    Remove,
};

SceneModel* create();
void destroy(SceneModel*);

/**
 * Recycles some memory from the scene model.
 * Should be called sparsly, after a lot of operations have
 * been done to the scene model.
 */
void defrag(SceneModel*);

/**
 * Registers an element at the given path root.
 * Note that for singular elements, this does nothing.
 */
void register_element(SceneModel*, const hrz_proto::PathRoot&);

/**
 * Unregisters an element at the given path root.
 * Note that for singular elements, this does nothing.
 */
void unregister_element(SceneModel*, const hrz_proto::PathRoot&);

/**
 * Get raw serialized data from the scene model at a given path.
 */
std::string get_raw(const SceneModel*, const hrz_proto::Path&);

/**
 * Set data on the scene model at a given path using raw serialized data.
 */
void set_raw(SceneModel*, const hrz_proto::Path&, std::string_view raw);

/**
 * Count the number of elements in a repeated field pointed to by the given
 * path.
 * If the path does not point to a valid repeated field, returns 0.
 */
uint32_t count(const SceneModel*, const hrz_proto::Path&);

/**
 * Adds an element to a repeated field pointed to by the given path
 * using raw serialized data.
 * Returns the new number of elements in the field.
 */
uint32_t add_raw(SceneModel*, const hrz_proto::Path&, std::string_view raw);

/**
 * Removes an element from a repeated field pointed to by the given
 * path. Returns the new number of elements in the field.
 */
uint32_t remove(SceneModel*, const hrz_proto::Path&);

/**
 * Get data from the scene model at a given path.
 * Returns whether retrieval was successful or not.
 */
template<typename T>
bool get(const SceneModel* model, const hrz_proto::Path& path, T& output)
{
    assert(model);
    std::string raw = get_raw(model, path);
    return output.ParseFromString(raw);
}

/**
 * Sets data in the scene model at a given path.
 */
template<typename T>
void set(SceneModel* model, const hrz_proto::Path& path, const T& input)
{
    assert(model);
    std::string raw = input.SerializeAsString();
    set_raw(model, path, raw);
}

/**
 * Adds an element to a repeated field in the scene model
 * at the given path.
 * Returns the new number of elements in the array.
 */
template<typename T>
uint32_t add(SceneModel* model, const hrz_proto::Path& path, const T& input)
{
    assert(model);
    std::string raw = input.SerializeAsString();
    return add_raw(model, path, raw);
}

void dev_ui(const SceneModel* model, mu_Context* ctx);

} // namespace scene_model

/**
 * To use with hrz_proto path builders.
 */
class SceneModelAccessor
{
    SceneModel* _model;

public:
    SceneModelAccessor() = default;

    explicit SceneModelAccessor(SceneModel* model) : _model(model) {}

    void set_raw(hrz_proto::Path&& path, std::string_view raw)
    {
        scene_model::set_raw(_model, path, raw);
    }

    template<typename T>
    void set(hrz_proto::Path&& path, const T& payload)
    {
        scene_model::set(_model, path, payload);
    }

    std::string get_raw(hrz_proto::Path&& path) { return scene_model::get_raw(_model, path); }

    template<typename T>
    T get(hrz_proto::Path&& path)
    {
        T msg;
        scene_model::get(_model, path, msg);
        return msg;
    }

    uint32_t add_raw(hrz_proto::Path&& path, std::string_view raw)
    {
        return scene_model::add_raw(_model, path, raw);
    }

    template<typename T>
    uint32_t add(hrz_proto::Path&& path, const T& payload)
    {
        return scene_model::add(_model, path, payload);
    }

    uint32_t remove(hrz_proto::Path&& path) { return scene_model::remove(_model, path); }

    uint32_t count(hrz_proto::Path&& path) { return scene_model::count(_model, path); }
};

/**
 * To use with hrz_proto path builders.
 */
class ConstSceneModelAccessor
{
    const SceneModel* _model;

public:
    ConstSceneModelAccessor() = default;

    explicit ConstSceneModelAccessor(const SceneModel* model) : _model(model) {}

    std::string get_raw(hrz_proto::Path&& path) { return scene_model::get_raw(_model, path); }

    template<typename T>
    T get(hrz_proto::Path&& path)
    {
        T msg;
        scene_model::get(_model, path, msg);
        return msg;
    }

    uint32_t count(hrz_proto::Path&& path) { return scene_model::count(_model, path); }
};

} // namespace hrz
