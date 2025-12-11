{% for f in protocol.files %}
#include "hrz/core/scene_path/{{ f.name[13:] }}_paths.h"
{% endfor %}

#include <utility>
#include <fmt/format.h>

namespace hrz::scene_model
{

std::string scene_model_path_to_string_generic(
    std::span<const uint32_t> parts,
    hrz::function_ref<void(std::string&, std::span<const uint32_t>)> first_to_string_inner)
{
    std::string out;
    first_to_string_inner(out, parts);
    return out;
}

{% for type in path_types if not type.is_path_leaf and not type.is_enum and not type.is_primitive %}
{% set PathType = type.full_name|to_short_type_name + "Path" %}

{% if type.is_path_root %}

{% if type.path_root_type != "HrzProtocol.Void" %}
::{{ type.path_root_type|rejoin(".", "::") }} {{ PathType }}::get_root() const
{
    assert(_path.has_root());
    return this->_path.root().{{ type.path_root }}();
}

{% endif %}
{% endif %}
void {{ PathType }}::to_string_inner(std::string& out, std::span<const uint32_t> parts)
{
    if (parts.empty()) return;

    switch (parts[0])
    {
    {% for f in type.fields %}
        case {{ f.id }}:
    {% if f.repeated %}
        {
            if (parts.size() >= 2)
            {
                fmt::format_to(std::back_inserter(out), "/{{ f.name }}[{}]", parts[1]);
                {{ f.type|to_short_type_name }}Path::to_string_inner(out, parts.subspan(2));
            }
            else
            {
                out += "/{{ f.name }}[*]";
            }
            break;
        }
    {% else %}
            out += "/{{ f.name }}";
            {{ f.type|to_short_type_name }}Path::to_string_inner(out, parts.subspan(1));
            break;
    {% endif %}
    {% endfor %}
        default: assert(false && "Invalid case"); break;
    }
}

{% for f in type.fields %}
{% set FieldPathType = f.type|to_short_type_name + "Path" %}

bool {{ PathType }}::is_{{ f.name }}() const
{
    return valid() && !leaf() && _path.parts(_index + 1) == {{ f.id }};
}

{% if not f.repeated %}
{{ FieldPathType }} {{ PathType }}::{{ f.name }}()
{
    assert(is_{{ f.name }}());
    return {{ FieldPathType }}(std::move(_path), _index + 1);
}

{% else %}
bool {{ PathType }}::has_{{ f.name }}_index() const
{
    if (!is_{{ f.name }}()) return false;
    return _path.parts_size() > _index + 2;
}

/* Return the index of the {{ f.name }} this path points to. */
uint32_t {{ PathType }}::{{ f.name }}_index() const
{
    assert(is_{{ f.name }}());
    return _path.parts(_index + 2);
}

{{ FieldPathType }} {{ PathType }}::{{ f.name }}() &&
{
    assert(is_{{ f.name }}());
    unsigned int shift = has_{{ f.name }}_index() ? 2 : 1;
    return {{ FieldPathType }}(std::move(_path), _index + shift);
}

{% endif %}
{% endfor %}

{% endfor %}
}
