#include "hrz/core/scene_model_array_sync.h"

#include "hrz/core/scene_model.h"
#include "hrz/core/scene_path/layer/three_d_tiles_layer_paths.h"
#include "hrz/protocol/path_builder/layer/three_d_tiles_layer.h"

#include <gtest/gtest.h>

struct Traits
{
    enum ElementUpdateType
    {
        All,
        Palette,
    };

    using ContainerPath = hrz::scene_model::ThreeDTilesLayerPath;
    using ElementPath = hrz::scene_model::MaterialPath;

    static inline bool has_element_index(const ContainerPath& path)
    {
        return path.has_materials_index();
    }

    static inline size_t element_index(const ContainerPath& path) { return path.materials_index(); }

    static inline ElementPath element_path(const ContainerPath& path)
    {
        return path.clone().materials();
    }

    static inline ElementUpdateType element_update_type(const ElementPath& path)
    {
        if (path.is_data_texture_palette())
        {
            return ElementUpdateType::Palette;
        }
        else
        {
            return ElementUpdateType::All;
        }
    }
};

struct SceneModelArraySync : public ::testing::Test
{
    using ArraySync = hrz::scene_model::ArraySync<Traits>;

    hrz_proto::ThreeDTilesLayer _container;
    ArraySync _sync;
    std::vector<std::string> _mirror;

    struct AccessorProxy
    {
        ArraySync* _sync;

        AccessorProxy(ArraySync* sync = nullptr) : _sync(sync) {}

        template<typename T>
        uint32_t add(const hrz_proto::Path& path, const T&)
        {
            _sync->notify_model_update(
                hrz::scene_model::UpdateType::Add, hrz::scene_model::ThreeDTilesLayerPath(path));
            return 0;
        }

        template<typename T>
        void set(const hrz_proto::Path& path, const T&)
        {
            _sync->notify_model_update(
                hrz::scene_model::UpdateType::Set, hrz::scene_model::ThreeDTilesLayerPath(path));
        }

        void set_raw(hrz_proto::Path&& path, std::string_view raw)
        {
            _sync->notify_model_update(
                hrz::scene_model::UpdateType::Set, hrz::scene_model::ThreeDTilesLayerPath(path));
        }

        uint32_t remove(hrz_proto::Path&& path)
        {
            _sync->notify_model_update(
                hrz::scene_model::UpdateType::Remove, hrz::scene_model::ThreeDTilesLayerPath(path));
            return 0;
        }
    };

    void set_materials(std::initializer_list<const char*> names)
    {
        auto* materials = _container.mutable_materials();
        materials->Clear();

        for (const char* name : names)
        {
            hrz_proto::Material* material = materials->Add();
            material->set_name(name);
        }

        hrz_proto::ThreeDTilesLayerPathBuilder<AccessorProxy>(
            AccessorProxy(&this->_sync), hrz_proto::LayerHandle())
            .set(_container);
    }

    void add_material(const char* material_name)
    {
        hrz_proto::Material* material = _container.add_materials();
        material->set_name(material_name);

        hrz_proto::ThreeDTilesLayerPathBuilder<AccessorProxy>(
            AccessorProxy(&this->_sync), hrz_proto::LayerHandle())
            .add_materials(*material);
    }

    void update_material(size_t index, const char* material_name)
    {
        _container.mutable_materials(index)->set_name(material_name);

        hrz_proto::ThreeDTilesLayerPathBuilder<AccessorProxy>(
            AccessorProxy(&this->_sync), hrz_proto::LayerHandle())
            .materials((uint32_t)index)
            .name()
            .set(material_name);
    }

    void remove_material(size_t index)
    {
        _container.mutable_materials()->erase(_container.mutable_materials()->begin() + index);

        hrz_proto::ThreeDTilesLayerPathBuilder<AccessorProxy>(
            AccessorProxy(&this->_sync), hrz_proto::LayerHandle())
            .remove_materials((uint32_t)index);
    }

    bool is_synchronized()
    {
        if ((size_t)_container.materials_size() != _mirror.size()) return false;

        for (size_t i = 0; i < (size_t)_container.materials_size(); ++i)
        {
            if (_container.materials(i).name() != _mirror[i]) return false;
        }

        return true;
    }

    bool check_content(std::initializer_list<const char*> names)
    {
        if (names.size() != _mirror.size()) return false;

        auto it = names.begin();
        for (size_t i = 0; i < names.size(); ++i, ++it)
        {
            if (*it != _mirror[i]) return false;
        }

        return true;
    }

    bool synchronize()
    {
        using Command = ArraySync::Command;

        _sync.synchronize(
            [&](const Command& cmd) -> size_t
            {
                switch (cmd.type)
                {
                    case Command::Add:
                    {
                        _mirror.push_back(_container.materials(cmd.info.add.index_auth).name());
                        break;
                    }
                    case Command::Update:
                    {
                        _mirror[cmd.info.update.index_mirror] =
                            _container.materials(cmd.info.update.index_auth).name();
                        break;
                    }
                    case Command::Remove:
                    {
                        _mirror.erase(_mirror.begin() + cmd.info.remove.index_mirror);
                        break;
                    }
                    case Command::RebuildAll:
                    {
                        _mirror.clear();
                        for (const auto& mat : _container.materials())
                        {
                            _mirror.push_back(mat.name());
                        }
                        break;
                    }
                }

                return _mirror.size();
            });

        return is_synchronized();
    }
};

TEST_F(SceneModelArraySync, SetAll)
{
    set_materials({"mat0", "mat1"});
    ASSERT_TRUE(synchronize());

    set_materials({"mat2"});
    ASSERT_TRUE(synchronize());

    set_materials({});
    ASSERT_TRUE(synchronize());

    set_materials({"mat0", "mat1", "mat3"});
    ASSERT_TRUE(synchronize());
}

TEST_F(SceneModelArraySync, Add)
{
    add_material("mat0");
    ASSERT_TRUE(synchronize());

    add_material("mat1");
    add_material("mat2");
    add_material("mat3");
    ASSERT_TRUE(synchronize());

    add_material("mat4");
    ASSERT_TRUE(synchronize());
}

TEST_F(SceneModelArraySync, SetAllThenAdd)
{
    set_materials({"mat0", "mat1"});
    ASSERT_TRUE(synchronize());

    add_material("mat5");
    set_materials({"mat2", "mat6"});
    add_material("mat3");
    add_material("mat4");
    ASSERT_TRUE(synchronize());

    add_material("mat4");
    ASSERT_TRUE(synchronize());
}

TEST_F(SceneModelArraySync, Remove)
{
    set_materials({"mat0", "mat1", "mat2", "mat3"});
    ASSERT_TRUE(synchronize());

    remove_material(1);
    ASSERT_TRUE(synchronize());

    set_materials({"mat0", "mat1", "mat2", "mat3"});
    remove_material(1);
    ASSERT_TRUE(synchronize());

    set_materials({"mat0", "mat1", "mat2", "mat3"});
    ASSERT_TRUE(synchronize());

    remove_material(0);
    remove_material(1);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"mat1", "mat3"}));

    set_materials({"mat0", "mat1", "mat2", "mat3"});
    ASSERT_TRUE(synchronize());

    remove_material(2);
    remove_material(0);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"mat1", "mat3"}));
}

TEST_F(SceneModelArraySync, AddAndRemove1)
{
    add_material("0");
    add_material("1");
    add_material("2");
    remove_material(1);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "2"}));
}

TEST_F(SceneModelArraySync, AddAndRemove2)
{
    add_material("0");
    add_material("1");
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "1"}));

    add_material("2");
    remove_material(1);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "2"}));

    remove_material(0);
    add_material("3");
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"2", "3"}));

    remove_material(1);
    remove_material(0);
    add_material("4");
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"4"}));
}

TEST_F(SceneModelArraySync, AddAndRemove3)
{
    set_materials({"0", "1", "2", "3", "4"});
    ASSERT_TRUE(synchronize());

    remove_material(1);
    remove_material(2);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "2", "4"}));
}

TEST_F(SceneModelArraySync, AddAndRemove4)
{
    set_materials({"0", "1", "2", "3", "4"});
    ASSERT_TRUE(synchronize());

    remove_material(3);
    remove_material(1);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "2", "4"}));
}

TEST_F(SceneModelArraySync, AddAndRemove5)
{
    set_materials({"0", "1", "2"});
    ASSERT_TRUE(synchronize());

    add_material("3");
    remove_material(1);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "2", "3"}));
}

TEST_F(SceneModelArraySync, AddAndRemove6)
{
    set_materials({"0", "1", "2"});
    ASSERT_TRUE(synchronize());

    remove_material(1);
    add_material("3");
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "2", "3"}));
}

TEST_F(SceneModelArraySync, AddAndRemove7)
{
    set_materials({"0", "1", "2"});
    ASSERT_TRUE(synchronize());

    remove_material(2);
    add_material("3");
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "1", "3"}));
}

TEST_F(SceneModelArraySync, AddAndRemove8)
{
    set_materials({"0", "1", "2"});
    ASSERT_TRUE(synchronize());

    add_material("3");
    remove_material(3);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "1", "2"}));
}

TEST_F(SceneModelArraySync, AddAndRemove9)
{
    set_materials({"0", "1", "2"});
    ASSERT_TRUE(synchronize());

    add_material("3");
    remove_material(1);
    remove_material(2);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "2"}));
}

TEST_F(SceneModelArraySync, AddAndRemove10)
{
    set_materials({"0", "1", "2"});
    ASSERT_TRUE(synchronize());

    add_material("3");
    add_material("4");
    add_material("5");
    remove_material(0);
    remove_material(2);
    remove_material(3);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"1", "2", "4"}));
}

TEST_F(SceneModelArraySync, AddAndRemove11)
{
    set_materials({"0", "1", "2"});
    ASSERT_TRUE(synchronize());

    add_material("3");
    add_material("4");
    add_material("5");
    remove_material(3);
    remove_material(3);
    remove_material(3);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "1", "2"}));
}

TEST_F(SceneModelArraySync, AddAndRemove12)
{
    add_material("0");
    add_material("1");
    add_material("2");
    add_material("3");
    add_material("4");
    add_material("5");
    remove_material(4);
    remove_material(1);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "2", "3", "5"}));
}

TEST_F(SceneModelArraySync, AddAndUpdate1)
{
    add_material("0");
    add_material("1");
    add_material("2");
    add_material("3");
    ASSERT_TRUE(synchronize());

    update_material(0, "zero");
    update_material(2, "two");
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"zero", "1", "two", "3"}));
}

TEST_F(SceneModelArraySync, AddAndUpdate2)
{
    add_material("0");
    add_material("1");
    add_material("2");
    add_material("3");
    update_material(0, "zero");
    update_material(2, "two");
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"zero", "1", "two", "3"}));
}

TEST_F(SceneModelArraySync, UpdateAndRemove1)
{
    add_material("0");
    add_material("1");
    add_material("2");
    add_material("3");
    update_material(3, "three");
    remove_material(3);
    update_material(1, "one");
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "one", "2"}));
}

TEST_F(SceneModelArraySync, UpdateAndRemove2)
{
    add_material("0");
    add_material("1");
    add_material("2");
    add_material("3");
    ASSERT_TRUE(synchronize());

    update_material(3, "three");
    remove_material(3);
    update_material(1, "one");
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "one", "2"}));
}

TEST_F(SceneModelArraySync, UpdateAndRemove3)
{
    add_material("0");
    add_material("1");
    add_material("2");
    add_material("3");
    ASSERT_TRUE(synchronize());

    remove_material(1);
    update_material(2, "3_");
    update_material(1, "2_");
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "2_", "3_"}));
}

TEST_F(SceneModelArraySync, UpdateAndRemove4)
{
    add_material("0");
    add_material("1");
    add_material("2");
    add_material("3");
    remove_material(1);
    update_material(2, "3_");
    update_material(1, "2_");
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0", "2_", "3_"}));
}

TEST_F(SceneModelArraySync, UpdateAndRemove5)
{
    add_material("0");
    add_material("1");
    add_material("2");
    add_material("3");
    ASSERT_TRUE(synchronize());

    update_material(3, "3_");
    update_material(2, "2_");
    update_material(1, "1_");
    update_material(0, "0_");
    remove_material(1);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0_", "2_", "3_"}));
}

TEST_F(SceneModelArraySync, UpdateAndRemove6)
{
    add_material("0");
    add_material("1");
    add_material("2");
    add_material("3");
    update_material(3, "3_");
    update_material(2, "2_");
    update_material(1, "1_");
    update_material(0, "0_");
    remove_material(1);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0_", "2_", "3_"}));
}

TEST_F(SceneModelArraySync, UpdateAndRemove7)
{
    add_material("0");
    add_material("1");
    ASSERT_TRUE(synchronize());

    add_material("2");
    add_material("3");
    update_material(3, "3_");
    update_material(2, "2_");
    update_material(1, "1_");
    update_material(0, "0_");
    remove_material(1);
    ASSERT_TRUE(synchronize());
    ASSERT_TRUE(check_content({"0_", "2_", "3_"}));
}
