+++
title = '{{ this.full_name|without_first(".") }}'
toc_start_level = 5
toc_end_level = 5

{% for field in this.fields %}
[[custom_toc]]
    title = '{{ field.name }}'
    anchor = 'field-{{ field.name }}'
{% endfor %}
+++

{% from "type_link.tpl.md" import type_link %}
{% from "anchor.tpl.md" import anchor %}

## Message {{ type_link(this.full_name) }}

*Defined in [`{{ this.file }}.proto`]({{ this.file | path_to_snake_case }}_proto.html).*

{{ this.documentation }}

{% for field in this.fields %}
---

##### `{{ field.name }}`: {{ type_link(field.type) }}{% if field.repeated %}`[]`{% endif -%}
    {%- if not field.union is sameas None %} (Part of union `{{ field.union }}`){% endif -%}
    {%- if field.optional %} *(Optional)*{% endif -%}
    {%- if field.deprecated %} *(DEPRECATED)*{% endif %} {{ anchor("field-" ~ field.name) }}

{{ field.documentation }}

{% endfor %}
