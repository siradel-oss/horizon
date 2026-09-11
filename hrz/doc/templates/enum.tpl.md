+++
title = '{{ this.full_name|without_first(".") }}'
toc_start_level = 5
toc_end_level = 5

{% for value in this["values"] %}
[[custom_toc]]
    title = '{{ value.name }}'
    anchor = 'value-{{ value.name }}'
{% endfor %}
+++

{% from "type_link.tpl.md" import type_link %}
{% from "anchor.tpl.md" import anchor %}

## Enumeration {{ type_link(this.full_name) }}

*Defined in [`{{ this.file }}.proto`]({{ this.file| path_to_snake_case }}_proto.html).*

{% if this.expose_to_style %}
*Available in [style scripts](../style_enums.html).*
{% endif %}

{{ this.documentation }}

{% for value in this["values"] %}
---

##### `{{ value.name }}` ({{ value.id }}){% if value.deprecated %} *(DEPRECATED)*{% endif %} {{ anchor("value-" ~ value.name) }}

{{ value.documentation }}

{% endfor %}
