#include <gsl/gsl-lite.hpp>

#include "hrz_descriptor_sets_list.h"

namespace
{
{% for id, descriptor_set in descriptor_sets.items() -%}
const unsigned char DescriptorSetData_{{ id }}[{{ descriptor_set["size"] }}] = { {{ descriptor_set["data_str"] }} };
{% endfor %}
} // namespace

namespace hrz::migration
{

DescriptorSetEntry DescriptorSets[DescriptorSetCount];

void initialize_descriptor_sets()
{
    int index = 0;

    {% for id, descriptor_set in descriptor_sets.items() -%}
    DescriptorSets[index++] = {
        0x{{ id }}u,
        gsl::span<const std::byte>((const std::byte*)DescriptorSetData_{{ id }}, {{ descriptor_set["size"] }})
    };
    {% endfor %}
}

} // namespace hrz
