+++
title = '{{ this.full_name|without_first(".") }}'
toc_start_level = 3
toc_end_level = 5

[[custom_toc]]
    title = 'Fields'
    anchor = 'fields'
{% for field in this.fields %}
{% if not field.union %}
[[custom_toc.children]]
    title = '{{ field.name }}'
    anchor = 'field-{{ field.name }}'
{% endif %}
{% endfor %}

{% for union in this.unions %}
[[custom_toc]]
    title = 'Union {{ union.name }}'
    anchor = 'union-{{ union.name }}'
{% for field in this.fields %}
{% if field.union == union.name %}
[[custom_toc.children]]
    title = '{{ field.name }}'
    anchor = 'field-{{ field.name }}'
{% endif %}
{% endfor %}
{% endfor %}

+++

{% from "type_link.tpl.md" import type_link %}
{% from "anchor.tpl.md" import anchor %}

## Message {{ type_link(this.full_name) }}

*Defined in [`{{ this.file }}.proto`]({{ this.file | path_to_snake_case }}_proto.html).*

{{ this.documentation }}

### Fields

{% for field in this.fields %}
{% if not field.union %}

##### `{{ field.name }}`: {{ type_link(field.type) }}{% if field.repeated %}`[]`{% endif -%}
    {%- if field.optional %} *(Optional)*{% endif -%}
    {%- if field.deprecated %} *(DEPRECATED)*{% endif %} {{ anchor("field-" ~ field.name) }}

{{ field.documentation }}

{% endif %}
{% endfor %}

{% for union in this.unions %}

### Union `{{ union.name }}` {{ anchor("union-" ~ union.name) }}
{{ union.documentation }}

{% for field in this.fields %}
{% if field.union == union.name %}

##### `{{ field.name }}`: {{ type_link(field.type) }}{% if field.repeated %}`[]`{% endif -%}
    {%- if field.deprecated %} *(DEPRECATED)*{% endif %} {{ anchor("field-" ~ field.name) }}

{{ field.documentation }}

{% endif %}
{% endfor %}
{% endfor %}
