#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <hrz_protocol_all.h>

namespace hrz
{

struct ClientMessageQueue;

namespace client_message_queue
{

{% set ns = namespace(typed_message_message=null, params_message_type="") %}

{% for m in protocol.messages %}
{% if m["full_name"] == "HrzProtocol.TypedMessage" %}
{% set ns.typed_message_message = m %}
{% endif %}
{% endfor %}

{% for e in protocol.enums %}
{% if e["full_name"] == "HrzProtocol.MessageType" %}
{% for v in e["values"] %}

{% for f in ns.typed_message_message["fields"] %}
{% if f["name"] == v.params_field_name %}
{% set ns.params_message_type = f["type"] %}
{% endif %}
{% endfor %}

    /**
     * Push a new message of type {{ v.name }}{{ " (\"" + v.label + "\")" if v.label != v.name }} at the end of the queue.
{% if v.documentation %}
     *
     * {{ v.documentation|indent_prefix(4, " * ") }}
{% endif %}
     */
    void enqueue_{{ v.name|lower }}(ClientMessageQueue*, {{ ns.params_message_type|rejoin(".", "::") }}&&);

{% endfor %}
{% endif %}
{% endfor %}

}

}
