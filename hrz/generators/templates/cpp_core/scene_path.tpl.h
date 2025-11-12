#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "hrz_core_scene_path_common.h"
#include "hrz_core_wrappers_scene_path.h"

#include "{{ filename }}.pb.h"
{% for f in dependencies %}
#include "{{ f }}.pb.h"
{% endfor %}

{% for f in dependencies %}
#include "hrz_core_{{ f[4:] }}_scene_path.h"
{% endfor %}

namespace hrz::scene_model
{

{% for type in path_types if (type.is_path_leaf or type.is_enum or type.is_primitive) and type.file == filename %}
{% set PathType = type.full_name|to_short_type_name + "Path" %}
using {{ PathType }} = SceneModelLeafPath<{{ type.full_name|to_cpp_qualified_name }}>;
{% endfor %}

{% for type in path_types if not type.is_path_leaf and not type.is_enum and not type.is_primitive and type.file == filename %}
{% set PathType = type.full_name|to_short_type_name + "Path" %}
class {{ PathType }} : public SceneModelPath<{{ PathType }}>
{
public:
    {% if type.is_path_root %}
    {{ PathType }}() = default;

    explicit {{ PathType }}(::hrz_proto::Path path) :
        SceneModelPath<{{ PathType }}>(std::move(path), -1)
    {
        assert(_path.has_root());
    }

    {% if type.path_root_type != "HrzProtocol.Void" %}
    ::{{ type.path_root_type|rejoin(".", "::") }} get_root() const;
    {% endif %}
    {% endif %}

    {{ PathType }}(hrz_proto::Path path, int index) :
        SceneModelPath<{{ PathType }}>(std::move(path), index)
    {
    }

    static void to_string_inner(std::string&, std::span<const uint32_t>);

    std::string to_string() const
    {
        return scene_model_path_to_string_generic(
            std::span<const uint32_t>(this->_path.parts().data(), (size_t)this->_path.parts().size()),
            to_string_inner);
    }

    {% for f in type.fields %}

    /* Return true if this path points to either {{ f.name }} or a child of it. */
    bool is_{{ f.name }}() const;

    {% if not f.repeated %}
    /**
     *  Go down the hierarchy through the field {{ f.name }}.
     *  A new instance is returned and this instance gets invalidated.
     */
    {{ f.type|to_short_type_name }}Path {{ f.name }}();
    {% else %}

    /* Return true if this path contains an index */
    bool has_{{ f.name }}_index() const;

    /* Return the index of the {{ f.name }} this path points to. */
    uint32_t {{ f.name }}_index() const;

    /**
     *  Go down the hierarchy through the field {{ f.name }}.
     *  A new instance is returned and this instance gets invalidated.
     */
    {{ f.type|to_short_type_name }}Path {{ f.name }}() &&;
    {% endif %}

    {% endfor %}
};

{% endfor %}

} // namespace hrz::scene_model
