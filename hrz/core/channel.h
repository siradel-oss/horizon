#pragma once

#include "hrz/fnd/class.h"
#include "hrz/fnd/defines.h"
#include "hrz/fnd/thread.h"

#include <cassert>
#include <deque>
#include <mutex>
#include <utility>

namespace hrz
{
template<typename TSend, typename TReceive>
struct Channel;

namespace channel
{
// Staging the messages when receiving them avoids a potential infinite
// loop if one side produces messages faster than the other side can
// receive them.
// It also allows taking the mutex only once when receiving.
template<typename TMessage>
struct StagedMessages
{
private:
    std::deque<TMessage>* queue = nullptr;
#if HRZ_DEBUG
    bool* is_staged = nullptr;
#endif

public:
    explicit StagedMessages(
        std::deque<TMessage>* queue
#if HRZ_DEBUG
        ,
        bool* is_staged
#endif
        ) :
        queue(queue)
#if HRZ_DEBUG
        ,
        is_staged(is_staged)
#endif
    {
#if HRZ_DEBUG
        assert(*is_staged);
#endif
    }

    HRZ_DELETE_COPY_MOVE(StagedMessages);

    ~StagedMessages()
    {
        queue->clear();
#if HRZ_DEBUG
        *is_staged = false;
#endif
    }

    auto begin() const { return queue->begin(); }

    auto end() const { return queue->end(); }
};

/**
 * Mediates the exchange of messages between the two sides of channel
 * in one direction.
 *
 * Creation and destruction of instances of this structure are managed
 * automatically when channels are created and closed.
 */
template<typename TMessage>
struct Broker
{
private:
    std::deque<TMessage> messages;
    std::deque<TMessage> staged_messages;
#if HRZ_DEBUG
    bool has_staged_messages = false;
#endif
    std::mutex message_mutex;

    bool side_a_is_closed = false;
    bool side_b_is_closed = false;
    std::mutex close_mutex;

    template<typename TSend, typename TReceive>
    friend struct ::hrz::Channel;
};
} // namespace channel

/**
 * One side of a bi-directionnal communication channel.
 *
 * Instances of this structure can be moved but not copied.
 * They can even be sent through another channel.
 *
 * Messages must be movable and it is recommended that they
 * are values objects.
 *
 * The intended way to handle multiple types of messages for
 * a single channel direction is to use a variant type.
 */
template<typename TSend, typename TReceive>
struct Channel
{
private:
    channel::Broker<TSend>* send_broker;
    channel::Broker<TReceive>* receive_broker;

    bool is_side_a; // Is side B if false.

    Channel(
        channel::Broker<TSend>* send_broker,
        channel::Broker<TReceive>* receive_broker,
        bool is_side_a) :
        send_broker(send_broker), receive_broker(receive_broker), is_side_a(is_side_a)
    {
    }

public:
    Channel() : send_broker(nullptr), receive_broker(nullptr), is_side_a(false) {}

    Channel(Channel&& other) :
        send_broker(std::exchange(other.send_broker, nullptr)),
        receive_broker(std::exchange(other.receive_broker, nullptr)),
        is_side_a(other.is_side_a)
    {
    }

    HRZ_DELETE_COPY(Channel);

    ~Channel() { close(); }

    Channel& operator=(Channel&& other)
    {
        if (this != &other)
        {
            close();

            send_broker = std::exchange(other.send_broker, nullptr);
            receive_broker = std::exchange(other.receive_broker, nullptr);
            is_side_a = other.is_side_a;
        }

        return *this;
    }

    void send(TSend&& message)
    {
        assert(send_broker != nullptr);
        if (send_broker == nullptr) return;

        HRZ_SCOPED_LOCK(send_broker->message_mutex);
        send_broker->messages.push_back(std::move(message));
    }

    // The returned structure is iterable, so retrieve messages by doing
    // `for (auto& message : channel.receive())`.
    // Do not call this method if the instance has been explicitly closed
    // before.
    channel::StagedMessages<TReceive> receive() &
    {
        assert(receive_broker != nullptr);

        HRZ_SCOPED_LOCK(receive_broker->message_mutex);
        assert(receive_broker->staged_messages.empty());

#if HRZ_DEBUG
        assert(!receive_broker->has_staged_messages);
        receive_broker->has_staged_messages = true;
#endif

        std::swap(receive_broker->messages, receive_broker->staged_messages);

        return channel::StagedMessages(
            &receive_broker->staged_messages
#if HRZ_DEBUG
            ,
            &receive_broker->has_staged_messages
#endif
        );
    }

    bool is_closed() const
    {
        // Both brokers are handled similarly in `close()`,
        // so it's fine to test just one.
        return send_broker == nullptr
            || (is_side_a ? send_broker->side_b_is_closed : send_broker->side_a_is_closed);
    }

    // Closes the channel explicitly.
    // Do not call `receive()` after this method has been called.
    void close()
    {
        if (send_broker == nullptr) return;

        bool destroy_send_broker = false;
        bool destroy_receive_broker = false;

        if (is_side_a)
        {
            {
                HRZ_SCOPED_LOCK(send_broker->close_mutex);
                send_broker->side_a_is_closed = true;
                destroy_send_broker = send_broker->side_b_is_closed;
            }
            {
                HRZ_SCOPED_LOCK(receive_broker->close_mutex);
                receive_broker->side_a_is_closed = true;
                destroy_receive_broker = receive_broker->side_b_is_closed;
            }
        }
        else
        {
            {
                HRZ_SCOPED_LOCK(send_broker->close_mutex);
                send_broker->side_b_is_closed = true;
                destroy_send_broker = send_broker->side_a_is_closed;
            }
            {
                HRZ_SCOPED_LOCK(receive_broker->close_mutex);
                receive_broker->side_b_is_closed = true;
                destroy_receive_broker = receive_broker->side_a_is_closed;
            }
        }

        if (destroy_send_broker) delete send_broker;
        if (destroy_receive_broker) delete receive_broker;

        send_broker = nullptr;
        receive_broker = nullptr;
    }

    template<typename TAtoB, typename TBtoA>
    friend std::pair<Channel<TAtoB, TBtoA>, Channel<TBtoA, TAtoB>> create_channel();
};

/**
 * Create a bi-directionnal communication channel and returns both
 * sides of it.
 * The channel is closed as soon as one side is explicitly closed
 * or destroyed.
 */
template<typename TAtoB, typename TBtoA>
std::pair<Channel<TAtoB, TBtoA>, Channel<TBtoA, TAtoB>> create_channel()
{
    auto a_to_b_broker = new channel::Broker<TAtoB>();
    auto b_to_a_broker = new channel::Broker<TBtoA>();

    return {
        Channel<TAtoB, TBtoA>(a_to_b_broker, b_to_a_broker, true),
        Channel<TBtoA, TAtoB>(b_to_a_broker, a_to_b_broker, false)};
}
} // namespace hrz
