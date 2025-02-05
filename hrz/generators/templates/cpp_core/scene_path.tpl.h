#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <assert.h>
#include <cstdint>

#include <hrz_protocol_all.h>

namespace hrz
{

struct Scene;

namespace scene_model
{

struct SceneModelPath
{
    hrz_proto::Path _path;
    int             _index = -1;

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
};

{% for type in path_types %}
struct {{ type.full_name|to_short_type_name }}Path;
{% endfor %}

{% for type in path_types %}
struct {{ type.full_name|to_short_type_name }}Path : public SceneModelPath
{
    {% if type.is_path_root %}
    {{ type.full_name|to_short_type_name }}Path() = default;

    {{ type.full_name|to_short_type_name }}Path(const ::hrz_proto::Path& path);

    {% if type.path_root_type != "HrzProtocol.Void" %}
    ::{{ type.path_root_type|rejoin(".", "::") }} get_root() const;
    {% endif %}
    {% endif %}

    std::string to_string();

    {% if not type.is_path_leaf %}
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
    {{ f.type|to_short_type_name }}Path {{ f.name }}();
    {% endif %}

    {% endfor %}
    {% endif %}
    /* Return a new instance representing the same path. */
    {{ type.full_name|to_short_type_name }}Path clone() const;
};

{% endfor %}

}}
