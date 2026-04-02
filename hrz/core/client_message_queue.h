#pragma once

namespace HrzProtocol
{

class TypedMessage;

} // namespace HrzProtocol

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
void enqueue_message(ClientMessageQueue*, HrzProtocol::TypedMessage&&);

/**
 * Remove the the first (i.e. oldest) message from the queue.
 * If a message was available, it is written to the given output parameter and
 * the function returns true. If no message was available, the function returns false.
 */
bool dequeue_message(ClientMessageQueue*, HrzProtocol::TypedMessage*);

/**
 * Return the number of messages in the queue.
 * The returned value is indicative only, and a greater number of messages may be
 * obtainable by dequeueing them.
 */
unsigned int get_queue_size(const ClientMessageQueue*);

} // namespace client_message_queue

} // namespace hrz
