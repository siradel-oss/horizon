---
Title: API reference
Category: General
MenuCategory: service,scene_model
---

## Services

{% for m in services|sort(attribute = "full_name") %}
* [[{{ m.full_name|without_first(".") }}]]
{% endfor %}

## Scene model roots

{% for m in scene_model_roots|sort(attribute = "type") %}
* <code>[[PathRoot]].{{ m.root_field }}: [[{{ m.type|without_first(".") }}]]</code>
{% endfor %}

## Files

{% for f in files|sort(attribute = "name") %}
* [`{{ f.name }}.proto`]({{ f.name|path_to_snake_case }}_proto.html)
{% endfor %}
