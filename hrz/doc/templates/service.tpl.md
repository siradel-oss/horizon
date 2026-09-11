+++
title = '{{ this.full_name|without_first(".") }}'
toc_start_level = 5
toc_end_level = 5

{% for method in this.methods %}
[[custom_toc]]
    title = '{{ method.name }}'
    anchor = 'method-{{ method.name }}'
{% endfor %}
+++

{% from "type_link.tpl.md" import type_link %}
{% from "anchor.tpl.md" import anchor %}

## Service {{ type_link(this.full_name) }}

*Defined in [`{{ this.file }}.proto`]({{ this.file|path_to_snake_case }}_proto.html).*

{{ this.documentation }}

{% for method in this.methods %}

---

##### `{{ method.name }}`({{ type_link(method.input) }}) -> {{ type_link(method.output) }} {{ anchor("method-" ~ method.name) }}

{% if method.deprecated %}*(DEPRECATED)*{% endif %}

{{ method.documentation }}

{% endfor %}

