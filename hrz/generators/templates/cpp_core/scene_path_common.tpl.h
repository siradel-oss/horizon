#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <assert.h>
#include <cstdint>

#include <hrz_fnd_function_ref.h>
#include <hrz_protocol_all.h> // @Todo(1209) Replace with something that only imports Path

namespace hrz
{

struct Scene;

namespace scene_model
{

template<typename T>
class SceneModelPath
{
protected:
    hrz_proto::Path _path;
    int             _index = -1;

public:
    SceneModelPath() = default;

    SceneModelPath(hrz_proto::Path path, int index) :
        _path(std::move(path)),
        _index(index)
    {
    }

    /* Return true if the path is usable. */
    bool valid() const
    {
        return _path.parts_size() >= 0 &&
            _index >= -1 && _index < _path.parts_size();
    }

    /* Return true if the end of this path has been reached. */
    bool leaf() const
    {
        return valid() && _index == _path.parts_size() - 1;
    }

    /* Return a new instance representing the same path. */
    T clone() const
    {
        return T(_path, _index);
    }
};

template<typename T>
class SceneModelLeafPath : public SceneModelPath<SceneModelLeafPath<T>>
{
public:
    SceneModelLeafPath(hrz_proto::Path path, int index) :
        SceneModelPath<SceneModelLeafPath<T>>(std::move(path), index)
    {
    }

    static void to_string_inner(std::string&, std::span<const uint32_t>)
    {
    }

    std::string to_string() const
    {
        return "";
    }
};

std::string scene_model_path_to_string_generic(
    std::span<const uint32_t> parts,
    hrz::function_ref<void(std::string&, std::span<const uint32_t>)> first_to_string_inner);

// Here we forward-declare path builders for recursive types.
{% for type in to_forward_declare %}
class {{ type|to_short_type_name }}Path;
{% endfor %}

// This is for type-safety: so that path types defined as an alias of SceneModelLeafPath
// are not all aliases of each other.
template<int N> struct UniqueType {};

} // namespace scene_model
} // namespace hrz
