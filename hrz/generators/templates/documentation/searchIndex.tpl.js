document.addEventListener('DOMContentLoaded', function() {
    {% for page in doc_pages %}
        addSearchItem("{{ page.name }}.html", "Documentation > {{ page.title }}");
    {% endfor %}
    {% for m in services %}
        addSearchItem("{{ m.full_name }}.html", "API > {{ m.full_name|without_first(".") }}");
        {% for method in m.methods %}
            addSearchItem("{{ m.full_name }}.html#method-{{ method.name }}", "API > {{ m.full_name|without_first(".") }}.{{ method.name }}");
        {% endfor %}
    {% endfor %}
    {% for m in messages %}
        addSearchItem("{{ m.full_name }}.html", "Types > {{ m.full_name|without_first(".") }}");
    {% endfor %}
    {% for m in enums %}
        addSearchItem("{{ m.full_name }}.html", "Enums > {{ m.full_name|without_first(".") }}");
    {% endfor %}
});
