////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

export let SCENE_MODEL = {
    {% for type in path_types %}
        "{{ type.full_name }}": [
            {% for f in type.fields %}
                {"name": "{{ f.name|camel_case }}", "type": "{{ f.type }}"},
            {% endfor %}
        ],
    {% endfor %}
};

export let SCENE_MODEL_ROOTS = [
    {% for type in path_types %}
    {% if type.is_path_root %}
    "{{ type.full_name }}",
    {% endif %}
    {% endfor %}
];

export let ENUMS = {
    {% for e in protocol.enums %}
    "{{ e.full_name }}": [
        {% for v in e["values"] %}
        {
            "name": "{{ v.name }}",
            "value": "{{ v.id }}",
            "text": "{{ v.label }}",
        },
        {% endfor %}
    ],
    {% endfor %}
};

export let VERSION = "{{ version }}";
