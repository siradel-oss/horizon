#include "hrz_core.h"
#include "hrz_core_scene_model_accessor.h"

#include <hrz_fnd_log.h>

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

namespace hrz::scene_model
{

{% for type in path_types %}
{% if not type.is_primitive and not type.is_enum %}

std::string get_message_part_raw(
    const {{ type.full_name|to_cpp_qualified_name }}& obj,
    gsl::span<const uint32_t> path)
{
    if (path.empty())
    {
        return obj.SerializeAsString();
    }

    uint32_t this_root = path[0];
    path = path.subspan(1);

    switch (this_root)
    {
        {% if not type.is_path_leaf %}
        {% for f in type.fields %}
        {% if not f.is_primitive and not f.is_enum %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return get_message_part_raw(obj.{{ f.name }}(), path);
        {% else %}
        case {{ f.id }}:
        {
            assert(!path.empty() && "Repeated fields need a child index in Get");
            if (!path.empty())
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name }}_size())
                {
                    return get_message_part_raw(obj.{{ f.name }}(element_index), path.subspan(1));
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                    return std::string();
                }
            }
            else
            {
                HRZ_LOG_ERROR("Missing \"{{ f.name }}\" index in \"{{ type.full_name }}\"");
                return std::string();
            }
        }
        {% endif %}
        {% else %}
        {% if not f.repeated %}
        case {{ f.id }}:
        {
            ::HrzProtocol::{{ f.type|to_wrapper }} wrapper;
            wrapper.set_value(obj.{{ f.name }}());
            return wrapper.SerializeAsString();
        }
        {% else %}
        case {{ f.id }}:
        {
            assert(!path.empty() && "Repeated fields need a child index in Get");
            if (!path.empty())
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name }}_size())
                {
                    ::HrzProtocol::{{ f.type|to_wrapper }} wrapper;
                    wrapper.set_value(obj.{{ f.name }}(element_index));
                    return wrapper.SerializeAsString();
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                    return std::string();
                }
            }
            else
            {
                HRZ_LOG_ERROR("Missing \"{{ f.name }}\" index in \"{{ type.full_name }}\"");
                return std::string();
            }
        }
        {% endif %}
        {% endif %}
        {% endfor %}
        {% endif %}
        default:
        {
            HRZ_LOG_ERROR("Invalid path in \"{{ type.full_name }}\": {}", this_root);
            assert(0 && "Wrong path!");
            return std::string();
        }
    }
}

void set_message_part_raw(
    {{ type.full_name|to_cpp_qualified_name }}& obj,
    gsl::span<const uint32_t> path,
    std::string_view raw)
{
    if (path.empty())
    {
        obj.ParseFromArray(raw.data(), raw.size());
        return;
    }

    uint32_t this_root = path[0];
    path = path.subspan(1);

    switch (this_root)
    {
        {% if not type.is_path_leaf %}
        {% for f in type.fields %}
        {% if not f.is_primitive and not f.is_enum %}
        {% if not f.repeated %}
        case {{ f.id }}:
            set_message_part_raw(*obj.mutable_{{ f.name }}(), path, raw);
            break;
        {% else %}
        case {{ f.id }}:
            assert(!path.empty() && "Repeated fields need a child index in Set");
            if (!path.empty())
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name }}_size())
                {
                    set_message_part_raw(*obj.mutable_{{ f.name }}(element_index), path.subspan(1), raw);
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                }
            }
            else
            {
                HRZ_LOG_ERROR("Missing \"{{ f.name }}\" index in \"{{ type.full_name }}\"");
            }
            break;
        {% endif %}
        {% else %}
        {% if not f.repeated %}
        case {{ f.id }}:
        {
            ::HrzProtocol::{{ f.type|to_wrapper }} wrapper;
            wrapper.ParseFromArray(raw.data(), raw.size());
            {% if f.is_enum %}
            obj.set_{{ f.name }}(({{ f.type|to_cpp_qualified_name }})wrapper.value());
            {% else %}
            obj.set_{{ f.name }}(wrapper.value());
            {% endif %}
            break;
        }
        {% else %}
        case {{ f.id }}:
        {
            assert(!path.empty() && "Repeated fields need a child index in Set");
            if (!path.empty())
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name }}_size())
                {
                    ::HrzProtocol::{{ f.type|to_wrapper }} wrapper;
                    wrapper.ParseFromArray(raw.data(), raw.size());
                    {% if f.is_enum %}
                    obj.set_{{ f.name }}(element_index, ({{ f.type|to_cpp_qualified_name }})wrapper.value());
                    {% else %}
                    obj.set_{{ f.name }}(element_index, wrapper.value());
                    {% endif %}
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                }
            }
            else
            {
                HRZ_LOG_ERROR("Missing \"{{ f.name }}\" index in \"{{ type.full_name }}\"");
            }
            break;
        }
        {% endif %}
        {% endif %}
        {% endfor %}
        {% endif %}
        default:
        {
            HRZ_LOG_ERROR("Invalid path in \"{{ type.full_name }}\": {}", this_root);
            assert(0 && "Wrong path!");
            break;
        }
    }
}

uint32_t count_message_part(
    const {{ type.full_name|to_cpp_qualified_name }}& obj,
    gsl::span<const uint32_t> path)
{
    if (path.empty())
    {
        return 0;
    }

    uint32_t this_root = path[0];
    path = path.subspan(1);

    switch (this_root)
    {
        {% if not type.is_path_leaf %}
        {% for f in type.fields %}
        {% if not f.is_primitive and not f.is_enum %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return count_message_part(obj.{{ f.name }}(), path);
        {% else %}
        case {{ f.id }}:
            if (path.empty())
            {
                return (uint32_t)obj.{{ f.name }}_size();
            }
            else
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name }}_size())
                {
                    return count_message_part(obj.{{ f.name }}(element_index), path.subspan(1));
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                    return (uint32_t)obj.{{ f.name }}_size();
                }
            }
        {% endif %}
        {% else %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return 0;
        {% else %}
        case {{ f.id }}:
            if (path.empty())
            {
                return (uint32_t)obj.{{ f.name }}_size();
            }
        {% endif %}
        {% endif %}
        {% endfor %}
        {% endif %}
        default:
        {
            HRZ_LOG_ERROR("Invalid path in \"{{ type.full_name }}\": {}", this_root);
            assert(0 && "Wrong path!");
            return 0;
        }
    }
}

uint32_t add_message_part_raw(
    {{ type.full_name|to_cpp_qualified_name }}& obj,
    gsl::span<const uint32_t> path,
    std::string_view raw)
{
    if (path.empty())
    {
        return 0;
    }

    uint32_t this_root = path[0];
    path = path.subspan(1);

    switch (this_root)
    {
        {% if not type.is_path_leaf %}
        {% for f in type.fields %}
        {% if not f.is_primitive and not f.is_enum %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return add_message_part_raw(*obj.mutable_{{ f.name }}(), path, raw);
        {% else %}
        case {{ f.id }}:
            if (path.empty())
            {
                auto new_obj = obj.add_{{ f.name }}();
                new_obj->ParseFromArray(raw.data(), raw.size());
                return (uint32_t)obj.{{ f.name }}_size();
            }
            else
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name }}_size())
                {
                    return add_message_part_raw(*obj.mutable_{{ f.name }}(element_index), path.subspan(1), raw);
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                    return (uint32_t)obj.{{ f.name }}_size();
                }
            }
        {% endif %}
        {% else %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return 0;
        {% else %}
        case {{ f.id }}:
            if (path.empty())
            {
                ::HrzProtocol::{{ f.type|to_wrapper }} wrapper;
                wrapper.ParseFromArray(raw.data(), raw.size());
                {% if f.is_enum %}
                obj.add_{{ f.name }}(({{ f.type|to_cpp_qualified_name }})wrapper.value());
                {% else %}
                obj.add_{{ f.name }}(wrapper.value());
                {% endif %}
                return (uint32_t)obj.{{ f.name }}_size();
            }
        {% endif %}
        {% endif %}
        {% endfor %}
        {% endif %}
        default:
        {
            HRZ_LOG_ERROR("Invalid path in \"{{ type.full_name }}\": {}", this_root);
            assert(0 && "Wrong path!");
            return 0;
        }
    }
}

uint32_t remove_message_part(
    {{ type.full_name|to_cpp_qualified_name }}& obj,
    gsl::span<const uint32_t> path)
{
    if (path.empty())
    {
        return 0;
    }

    uint32_t this_root = path[0];
    path = path.subspan(1);

    switch (this_root)
    {
        {% if not type.is_path_leaf %}
        {% for f in type.fields %}
        {% if not f.is_primitive and not f.is_enum %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return remove_message_part(*obj.mutable_{{ f.name }}(), path);
        {% else %}
        case {{ f.id }}:
        {
            assert(!path.empty() && "Repeated paths need a child index in Remove");
            if (!path.empty())
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name }}_size())
                {
                    if (path.size() == 1)
                    {
                        auto field = obj.mutable_{{ f.name }}();
                        if (element_index < (uint32_t)field->size())
                        {
                            field->erase(field->begin() + element_index);
                        }

                        return (uint32_t)field->size();
                    }
                    else
                    {
                        uint32_t element_index = path[0];
                        return remove_message_part(*obj.mutable_{{ f.name }}(element_index), path.subspan(1));
                    }
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                    return (uint32_t)obj.{{ f.name }}_size();
                }
            }
            else
            {
                HRZ_LOG_ERROR("Missing \"{{ f.name }}\" index in \"{{ type.full_name }}\"");
                return (uint32_t)obj.{{ f.name }}_size();
            }
        }
        {% endif %}
        {% else %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return 0;
        {% else %}
        case {{ f.id }}:
        {
            assert(!path.empty() && "Repeated paths need a child index in Remove");
            if (!path.empty())
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name }}_size())
                {
                    if (path.size() == 1)
                    {
                        auto field = obj.mutable_{{ f.name }}();
                        if (element_index < (uint32_t)field->size())
                        {
                            field->erase(field->begin() + element_index);
                        }

                        return (uint32_t)field->size();
                    }
                    else
                    {
                        HRZ_LOG_ERROR("Invalid path in \"{{ type.full_name }}\": {}", this_root);
                        return (uint32_t)obj.{{ f.name }}_size();
                    }
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                    return (uint32_t)obj.{{ f.name }}_size();
                }
            }
            else
            {
                HRZ_LOG_ERROR("Missing \"{{ f.name }}\" index in \"{{ type.full_name }}\"");
                return (uint32_t)obj.{{ f.name }}_size();
            }
        }
        {% endif %}
        {% endif %}
        {% endfor %}
        {% endif %}
        default:
        {
            HRZ_LOG_ERROR("Invalid path in \"{{ type.full_name }}\": {}", this_root);
            assert(0 && "Wrong path!");
            return 0;
        }
    }
}

{% endif %}
{% endfor %}

}
