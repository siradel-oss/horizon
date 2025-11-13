#pragma once

#include "hrz/common/proto_maths.h"
#include "hrz/core/model/model.h"
#include "hrz/core/scene_model.h"
#include "hrz/core/scene_model_array_sync.h"
#include "hrz/core/scene_path/scene_path.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/gen_index_pool.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/string_utils.h"
#include "hrz/protocol/path_builder.h"

#include <algorithm>

namespace hrz::model
{
struct SingleModelMaterialManagerTraits
{
    using BakedModelHandleType = SingleBakedModelH;
    using ModelGeometryHandleType = SingleModelGeometryH;
};

struct BatchedModelMaterialManagerTraits
{
    using BakedModelHandleType = BatchedBakedModelH;
    using ModelGeometryHandleType = BatchedModelGeometryH;
};

// This is a utility for managing multiple materials.
// Is is used to synchronize materials in a materials array from the scene
// model, fetch material pairs for multi-material purposes, create baked
// models, etc.
// The traits are used to adapt this feature to different model
// representations (single, batched, etc).
template<typename Traits>
class MaterialsManager
{
    using BakedModelHandleType = typename Traits::BakedModelHandleType;
    using ModelGeometryHandleType = typename Traits::ModelGeometryHandleType;

    struct Material
    {
        explicit Material(const hrz_proto::Material& d = {}) : definition(d) {}

        bool created = false;
        hrz_proto::Material definition;
        hrz::model::ModelMaterialH material;
    };

    using MaterialHandle = uint32_t;
    using IndexPool = hrz::GenIndexPool<MaterialHandle, 12, 20>;
    using MaterialsPool = hrz::GenObjectPool<Material, IndexPool>;

    MaterialsPool _materials_pool;

    std::vector<MaterialHandle> _materials;
    MaterialHandle _default_material;

    using MaterialPair = std::pair<MaterialHandle, std::optional<MaterialHandle>>;
    hrz::flat_hash_map<MaterialPair, BakedModelHandleType> _models_cache;
    std::optional<BakedModelHandleType> _active_model;
    bool _needs_to_update_active_model = false;
    std::string _base_material_name;
    std::optional<std::string> _overlay_material_name;

    inline void delete_material(ModelPrototype* proto, MaterialHandle handle)
    {
        Material* material = _materials_pool.get_object(handle);
        assert(material);

        if (material->created)
        {
            model::destroy(proto, material->material);
        }
        _materials_pool.release(handle);
    }

    void delete_all_materials(ModelPrototype* proto)
    {
        assert(proto);

        for (MaterialHandle handle : _materials)
        {
            delete_material(proto, handle);
        }
        delete_material(proto, _default_material);
        _materials.clear();
    }

    std::optional<MaterialHandle> find_material_by_name(std::string_view name)
    {
        auto it = std::ranges::find_if(
            _materials,
            [&](const MaterialHandle handle)
            { return _materials_pool.get_object(handle)->definition.name() == name; });

        if (it != _materials.end())
        {
            return *it;
        }
        else
        {
            return std::nullopt;
        }
    }

    void destroy_cached_models_with_material(ModelPrototype* proto, MaterialHandle handle)
    {
        assert(proto);

        for (auto it = _models_cache.begin(); it != _models_cache.end();)
        {
            if (it->first.first == handle
                || (it->first.second.has_value() && it->first.second.value() == handle))
            {
                if (_active_model.has_value() && _active_model.value().o == it->second.o)
                {
                    _active_model.reset();
                    _needs_to_update_active_model = true;
                }
                hrz::model::destroy(proto, it->second);
                _models_cache.erase(it++);
            }
            else
            {
                it++;
            }
        }
    }

    void recreate_all_materials_inner(ModelPrototype* proto)
    {
        assert(proto);

        for (const auto& model : _models_cache)
        {
            model::destroy(proto, model.second);
        }
        _models_cache.clear();
        _active_model.reset();

        for (MaterialHandle handle : _materials)
        {
            delete_material(proto, handle);
        }
        _materials.clear();
        _needs_to_update_active_model = true;
    }

public:
    void add_material(const hrz_proto::Material& material)
    {
        _materials.push_back(_materials_pool.alloc(material));
        _needs_to_update_active_model = true;
    }

    void recreate_all_materials(
        ModelPrototype* proto,
        std::span<const hrz_proto::Material> materials)
    {
        recreate_all_materials_inner(proto);

        for (const auto& material : materials)
        {
            add_material(material);
        }
    }

    void recreate_all_materials(
        ModelPrototype* proto,
        std::span<const hrz_proto::Material* const> materials)
    {
        recreate_all_materials_inner(proto);

        for (const auto* material : materials)
        {
            add_material(*material);
        }
    }

    void remove_material(ModelPrototype* proto, size_t index)
    {
        assert(proto);

        MaterialHandle handle = _materials[index];

        destroy_cached_models_with_material(proto, handle);

        delete_material(proto, _materials[index]);
        _materials.erase(_materials.begin() + index);

        _needs_to_update_active_model = true;
    }

    void update_whole_material(
        ModelPrototype* proto,
        const hrz_proto::Material& material,
        size_t mirror_index)
    {
        assert(proto);

        MaterialHandle old_handle = _materials[mirror_index];

        destroy_cached_models_with_material(proto, old_handle);
        delete_material(proto, _materials[mirror_index]);

        _materials[mirror_index] = _materials_pool.alloc(material);

        _needs_to_update_active_model = true;
    }

    void update_material_palette(
        ModelPrototype* proto,
        const hrz_proto::NumericPalette& palette,
        size_t mirror_index)
    {
        assert(proto);

        Material* material = _materials_pool.get_object(_materials[mirror_index]);
        material->definition.mutable_data_texture_palette()->CopyFrom(palette);

        if (material->created)
        {
            model::update_data_texture_palette(
                proto, material->material, material->definition.data_texture_palette());
        }
    }

    void prototype_may_have_been_recreated(ModelPrototype* proto)
    {
        assert(_models_cache.size() == 0);
        assert(_materials.size() == 0);
        assert(!_active_model.has_value());

        if (proto)
        {
            _default_material = _materials_pool.alloc(Material{});
        }

        _needs_to_update_active_model = true;
    }

    void delete_all(ModelPrototype* proto)
    {
        for (const auto& model : _models_cache)
        {
            hrz::model::destroy(proto, model.second);
        }
        _models_cache.clear();
        _active_model.reset();
        delete_all_materials(proto);
    }

    void set_active_materials(
        std::string_view base_material,
        std::optional<std::string_view> overlay_material)
    {
        _base_material_name = std::string(base_material);

        if (overlay_material.has_value())
        {
            _overlay_material_name = std::string(overlay_material.value());
        }
        else
        {
            _overlay_material_name = std::nullopt;
        }

        _needs_to_update_active_model = true;
    }

    RenderRequest update(ModelPrototype* proto, ModelGeometryHandleType geometry)
    {
        RenderRequest render_request;

        if (_needs_to_update_active_model && proto)
        {
            MaterialHandle base_material_handle =
                find_material_by_name(_base_material_name).value_or(_default_material);

            std::optional<MaterialHandle> overlay_material_handle_opt = std::nullopt;
            if (_overlay_material_name.has_value())
            {
                overlay_material_handle_opt = find_material_by_name(_overlay_material_name.value())
                                                  .value_or(_default_material);
            }

            MaterialPair material_pair(base_material_handle, overlay_material_handle_opt);
            auto it = _models_cache.find(material_pair);

            if (it == _models_cache.end())
            {
                auto* base_material = _materials_pool.get_object(base_material_handle);
                if (!base_material->created)
                {
                    base_material->material =
                        model::create_model_material(proto, base_material->definition);
                    base_material->created = true;
                }

                std::optional<hrz::model::ModelMaterialH> overlay_model_material;
                if (overlay_material_handle_opt.has_value())
                {
                    auto* overlay_material =
                        _materials_pool.get_object(overlay_material_handle_opt.value());
                    if (!overlay_material->created)
                    {
                        overlay_material->material =
                            model::create_model_material(proto, overlay_material->definition);
                        overlay_material->created = true;
                    }
                    overlay_model_material = overlay_material->material;
                }

                BakedModelHandleType model = model::create_baked_model(
                    proto, geometry, base_material->material, overlay_model_material);

                _models_cache.insert(std::make_pair(material_pair, model));
                _active_model = model;
            }
            else
            {
                _active_model = it->second;
            }

            _needs_to_update_active_model = false;
            render_request.request_visual_render();
        }
        else
        {
            _needs_to_update_active_model = false;
        }

        return render_request;
    }

    inline void work(ModelPrototype* proto)
    {
        if (_active_model)
        {
            model::work(proto, _active_model.value());
        }
    }

    inline RenderRequest work_gpu(ModelPrototype* proto, SharedResources* sr, Render* render)
    {
        if (_active_model)
        {
            return model::work_gpu(proto, _active_model.value(), sr, render);
        }
        else
        {
            return {};
        }
    }

    inline bool is_working(ModelPrototype* proto)
    {
        if (_active_model)
        {
            return _active_model.has_value() && model::is_working(proto, _active_model.value());
        }
        else
        {
            return false;
        }
    }

    constexpr std::optional<BakedModelHandleType> get_active_model() const { return _active_model; }

    inline size_t material_count() const { return _materials.size(); }
};

} // namespace hrz::model
