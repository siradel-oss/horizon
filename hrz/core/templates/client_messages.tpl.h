#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

namespace hrz
{
struct ClientMessageQueue;
} // namespace hrz


{% for msg in client_messages %}
namespace {{ msg.message.package|rejoin(".", "::") }} { class {{ msg.message.name|rejoin(".", "::") }}; }
{% endfor %}


namespace hrz::client_message_queue
{
{% for msg in client_messages %}
    /**
     * Push a new message of type {{ msg.enum_value.name }} at the end of the queue.
{% if msg.enum_value.documentation %}
     *
     * {{ msg.enum_value.documentation|indent_prefix(4, " * ") }}
{% endif %}
     */
    void enqueue_{{ msg.enum_value.name|lower }}(ClientMessageQueue*, {{ msg.message.full_name|rejoin(".", "::") }}&&);

{% endfor %}
} // namespace hrz::client_message_queue
