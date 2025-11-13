#include "hrz/core/client_message_queue.h"

#include "hrz/fnd/thread.h"

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

std::optional<hrz_proto::TypedMessage> dequeue_message(ClientMessageQueue* queue)
{
    assert(queue);

    HRZ_SCOPED_LOCK(queue->mutex);

    if (queue->messages.empty()) return std::nullopt;

    auto message = queue->messages.front();
    queue->messages.pop();

    return {std::move(message)};
}

unsigned int get_queue_size(const ClientMessageQueue* queue)
{
    assert(queue);

    HRZ_SCOPED_LOCK(queue->mutex);

    return queue->messages.size();
}

} // namespace client_message_queue

} // namespace hrz
