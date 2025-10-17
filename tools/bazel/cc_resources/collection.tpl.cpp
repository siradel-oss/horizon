#include "{{ header }}"

{% for k in keys %}
extern const size_t _embedded_resource_{{ k }}_len;
extern const std::byte* _embedded_resource_{{ k }};
{% endfor %}

std::span<const std::byte> {{ namespace }}::get_data(Resources id)
{
    switch (id)
    {
        {% for k in keys %}
        case Resources::{{ k }}:
            return std::span<const std::byte>(
                _embedded_resource_{{ k }},
                _embedded_resource_{{ k }}_len);
        {% endfor %}
        default: return std::span<const std::byte>();
    }
}
