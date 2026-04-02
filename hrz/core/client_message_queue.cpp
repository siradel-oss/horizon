#include "hrz/core/client_message_queue.h"

#include "hrz/fnd/thread.h"
#include "hrz/protocol/message_queue/message.pb.h"

#include <mutex>
#include <queue>

namespace hrz
{

struct ClientMessageQueue
{
    std::queue<hrz_proto::TypedMessage> messages;
    mutable std::mutex mutex;
};

namespace client_message_queue
{

ClientMessageQueue* create()
{
    auto queue = new ClientMessageQueue();

    return queue;
}

void destroy(ClientMessageQueue* queue)
{
    assert(queue);

    delete queue;
}

void enqueue_message(ClientMessageQueue* queue, hrz_proto::TypedMessage&& message)
{
    assert(queue);

    HRZ_SCOPED_LOCK(queue->mutex);

    queue->messages.push(std::move(message));
}

bool dequeue_message(ClientMessageQueue* queue, hrz_proto::TypedMessage* out_message)
{
    assert(queue);

    HRZ_SCOPED_LOCK(queue->mutex);

    if (queue->messages.empty()) return false;

    *out_message = std::move(queue->messages.front());
    queue->messages.pop();

    return true;
}

unsigned int get_queue_size(const ClientMessageQueue* queue)
{
    assert(queue);

    HRZ_SCOPED_LOCK(queue->mutex);

    return (unsigned int)queue->messages.size();
}

} // namespace client_message_queue

} // namespace hrz
