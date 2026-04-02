document.addEventListener('DOMContentLoaded', function() {
    {% for page in doc_pages %}
        addSearchItem("{{ page.name }}.html", "{{ page.title }}", "Documentation &gt; {{ page.title }}");
    {% endfor %}
    {% for m in services %}
        addSearchItem("{{ m.full_name }}.html", "{{ m.full_name }}", "API &gt; {{ m.full_name|without_first(".")|wbr_on_period }}");
        {% for method in m.methods %}
            addSearchItem("{{ m.full_name }}.html#method-{{ method.name }}", "{{m.full_name}} {{ method.name }}", "API &gt; {{ m.full_name|without_first(".")|wbr_on_period }}.<wbr />{{ method.name }}");
        {% endfor %}
    {% endfor %}
    {% for m in messages %}
        addSearchItem("{{ m.full_name }}.html", "{{ m.full_name }}", "Types &gt; {{ m.full_name|without_first(".")|wbr_on_period }}");
    {% endfor %}
    {% for m in enums %}
        addSearchItem("{{ m.full_name }}.html", "{{ m.full_name }}", "Enums &gt; {{ m.full_name|without_first(".")|wbr_on_period }}");
    {% endfor %}
    {% for f in files %}
        addSearchItem("{{ f.name|path_to_snake_case }}_proto.html", "{{ f.name }}.proto", "{{ f.name|wbr_on_slash }}.proto");
    {% endfor %}
});
