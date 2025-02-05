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

| Type | Field in [[PathRoot]] |
|------|-----------------------|
{% for m in scene_model_roots|sort(attribute = "type") %}
| [[{{ m.type|without_first(".") }}]] | `{{ m.root_field }}` |
{% endfor %}
