// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

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
     * Push a new message of type {{ msg.name }} at the end of the queue.
     */
    void enqueue_{{ msg.name }}_message(ClientMessageQueue*, {{ msg.message.full_name|rejoin(".", "::") }}&&);

{% endfor %}
} // namespace hrz::client_message_queue
