#include "hrz/core/client_messages.h"

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "hrz/core/client_message_queue.h"

#include "hrz/protocol/message_queue/message.pb.h"

namespace hrz::client_message_queue
{

{% for m in client_messages %}
void enqueue_{{ m.enum_value.name|lower }}(ClientMessageQueue* queue, {{ m.message.full_name|rejoin(".", "::") }}&& message)
{
    {{ m.message.full_name|rejoin(".", "::") }} local_message(message);

    hrz_proto::TypedMessage full_message;
    full_message.set_type(hrz_proto::MessageType::{{ m.enum_value.name }});
    full_message.mutable_{{ m.enum_value.params_field_name }}()->Swap(&local_message);

    enqueue_message(queue, std::move(full_message));
}

{% endfor %}
} // namespace hrz::client_message_queue
