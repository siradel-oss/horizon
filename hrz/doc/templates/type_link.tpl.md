{%- macro type_link(full_name) -%}
    {%- for element in full_name|type_path -%}
        {%- if element.path is sameas "" -%}
            `{{ element.text }}`
        {%- else -%}
            .[`{{ element.text }}`]({{ element.path }}.html)
        {%- endif -%}
    {%- endfor -%}
{%- endmacro -%}
