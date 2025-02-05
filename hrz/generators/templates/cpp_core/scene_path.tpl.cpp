#include "hrz_core_scene_path.h"

#include <utility>

namespace hrz::scene_model
{
{% for type in path_types %}
{% if type.is_path_root %}
{{ type.full_name|to_short_type_name }}Path::{{ type.full_name|to_short_type_name }}Path(const ::hrz_proto::Path& path)
{
    assert(path.has_root());

    _path = ::hrz_proto::Path(path);
    _index = -1;
}

{% if type.path_root_type != "HrzProtocol.Void" %}
::{{ type.path_root_type|rejoin(".", "::") }} {{ type.full_name|to_short_type_name }}Path::get_root() const
{
    assert(_path.has_root());
    return this->_path.root().{{ type.path_root }}();
}

{% endif %}
{% endif %}
std::string {{ type.full_name|to_short_type_name }}Path::to_string()
{
    {% if type.is_path_leaf %}
    return "";
    {% else %}
    if (leaf()) return "";

    switch (_path.parts(_index + 1))
    {
    {% for f in type.fields %}
        case {{ f.id }}:
    {% if f.repeated %}
        {
            if (has_{{ f.name }}_index())
            {
                uint32_t index = {{ f.name }}_index();
                return "/{{ f.name }}["
                    + std::to_string(index)
                    +"]" + {{ f.name }}().to_string();
            }
            else
            {
                return "/{{ f.name }}";
            }
        }
    {% else %}
            return "/{{ f.name }}" + {{ f.name }}().to_string();
    {% endif %}
    {% endfor %}
        default: assert(false && "Invalid case"); break;
    }

    return "";
    {% endif %}
}

{% if not type.is_path_leaf %}
{% for f in type.fields %}
bool {{ type.full_name|to_short_type_name }}Path::is_{{ f.name }}() const
{
    return valid() && !leaf() && _path.parts(_index + 1) == {{ f.id }};
}

{% if not f.repeated %}
{{ f.type|to_short_type_name }}Path {{ type.full_name|to_short_type_name }}Path::{{ f.name }}()
{
    assert(is_{{ f.name }}());

    {{ f.type|to_short_type_name }}Path p;
    p._path = std::move(_path);
    p._index = _index + 1;
    return p;
}

{% else %}
bool {{ type.full_name|to_short_type_name }}Path::has_{{ f.name }}_index() const
{
    if (!is_{{ f.name }}()) return false;

    return _path.parts_size() > _index + 2;
}

/* Return the index of the {{ f.name }} this path points to. */
uint32_t {{ type.full_name|to_short_type_name }}Path::{{ f.name }}_index() const
{
    assert(is_{{ f.name }}());

    return _path.parts(_index + 2);
}

{{ f.type|to_short_type_name }}Path {{ type.full_name|to_short_type_name }}Path::{{ f.name }}()
{
    assert(is_{{ f.name }}());

    unsigned int shift = has_{{ f.name }}_index() ? 2 : 1;

    {{ f.type|to_short_type_name }}Path p;
    p._path = std::move(_path);
    p._index = _index + shift;
    return p;
}

{% endif %}
{% endfor %}
{% endif %}
{{ type.full_name|to_short_type_name }}Path {{ type.full_name|to_short_type_name }}Path::clone() const
{
    {{ type.full_name|to_short_type_name }}Path p;
    p._path = ::hrz_proto::Path(_path);
    p._index = _index;
    return p;
}

{% endfor %}
}
