#pragma once

#include <hrz_protocol_all.h>

#include <optional>

namespace hrz
{
struct ClientMessageQueue;

namespace client_message_queue
{
/**
 * Instantiates a new message queue.
 */
ClientMessageQueue* create();

/**
 * Free the given message queue.
 */
void destroy(ClientMessageQueue*);

/**
 * Push a new message at the end of the queue.
 */
void enqueue_message(ClientMessageQueue*, hrz_proto::TypedMessage&&);

/**
 * Remove the the first (i.e. oldest) message from the queue, and return it.
 */
std::optional<hrz_proto::TypedMessage> dequeue_message(ClientMessageQueue*);

/**
 * Return the number of messages in the queue.
 * The returned value is indicative only, and a greater number of messages may be
 * obtainable by dequeueing them.
 */
unsigned int get_queue_size(const ClientMessageQueue*);
} // namespace client_message_queue

} // namespace hrz
