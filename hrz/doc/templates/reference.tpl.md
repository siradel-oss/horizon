+++
title = "API reference"
layout = "single"

[cascade.params]
    title_suffix = "Horizon API Reference"

[[doc_menu]]
    name = "Services"
    entries = [
        {% for m in services|sort(attribute = "full_name") %}
        "{{ m.full_name }}",
        {% endfor %}
    ]
[[doc_menu]]
    name = "Scene model"
    entries = [
        {% for m in scene_model_roots|sort(attribute = "type") %}
        "{{ m.type }}",
        {% endfor %}
    ]
+++

# API reference

## Services

{% for m in services|sort(attribute = "full_name") %}
* [{{ m.full_name|without_first(".") }}]($proto)
{% endfor %}

## Scene model roots

{% for m in scene_model_roots|sort(attribute = "type") %}
* [PathRoot]($proto).`{{ m.root_field }}`: [{{ m.type|without_first(".") }}]($proto)
{% endfor %}

## Files

{% for f in files|sort(attribute = "name") %}
* [`{{ f.name }}.proto`]({{ f.name|path_to_snake_case }}_proto.html)
{% endfor %}
