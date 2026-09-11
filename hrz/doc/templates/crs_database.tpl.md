+++
title = "Known SRIDs"
+++

# Known SRIDs

The spatial reference systems in this list can be used using the short form descriptor "auth:srid".

{% for auth, crs_list in crs_db.items() %}
### {{ auth }}

| SRID | Name | PROJ.4 string |
|------|------|---------------|
{% for crs in crs_list -%}
| {% if auth == "EPSG" %} [{{ auth }}:{{ crs.srid }}](https://epsg.io/{{ crs.srid }}) {% else %} {{ auth }}:{{ crs.srid }} {% endif %} | {{ crs.name }} | `{{ crs.proj_str|trim }}` |
{% endfor %}

{% endfor %}
