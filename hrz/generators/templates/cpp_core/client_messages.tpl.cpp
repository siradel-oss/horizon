#include "hrz_core_client_messages.h"

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "hrz_core_client_message_queue.h"

namespace hrz
{

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

void enqueue_{{ v.name|lower }}(ClientMessageQueue* queue, {{ ns.params_message_type|rejoin(".", "::") }}&& message)
{
    {{ ns.params_message_type|rejoin(".", "::") }} local_message(message);

    hrz_proto::TypedMessage full_message;
    full_message.set_type(hrz_proto::MessageType::{{ v.name }});
    full_message.mutable_{{ v.params_field_name }}()->Swap(&local_message);

    enqueue_message(queue, std::move(full_message));
}

{% endfor %}
{% endif %}
{% endfor %}

}

}
