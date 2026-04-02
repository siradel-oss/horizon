#pragma once

#include "hrz/core/channel.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/thread.h"

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace hrz
{

/**
 * Maintains a group of channels, each one associated with a unique ID.
 *
 * Channels can be created in a thread-safe manner.
 *
 * The channels can be iterated over, like so:
 *     `for (auto& [channel_id, channel] : channel_group) { ... }`
 *
 * Channels that are closed are automatically removed from the group
 * when iterating over it. If the caller needs to react to closed
 * channels, call `is_closed()` while iterating and act accordingly.
 */
template<typename TSend, typename TReceive>
struct ChannelGroup
{
private:
    uint64_t next_channel_id = 0;
    hrz::flat_hash_map<uint64_t, Channel<TSend, TReceive>> channels;
    std::vector<std::pair<uint64_t, Channel<TSend, TReceive>>> new_channels;
    std::mutex new_channels_mutex;

public:
    /**
     * Creates a new channel, returning the side that should be sent to the
     * system that doesn't own this channel group, and inserting the other side
     * in the group.
     * The channel ID is also returned.
     *
     * This method is thread-safe.
     */
    std::pair<uint64_t, hrz::Channel<TReceive, TSend>> create_channel()
    {
        auto [group_channel, returned_channel] = hrz::create_channel<TSend, TReceive>();

        uint64_t channel_id;

        {
            HRZ_SCOPED_LOCK(new_channels_mutex);
            channel_id = next_channel_id;
            next_channel_id += 1;
            new_channels.emplace_back(channel_id, std::move(group_channel));
        }

        return {channel_id, std::move(returned_channel)};
    }

    /**
     * Maintains the channel group internal state.
     *
     * Call this method at every update.
     */
    void work()
    {
        HRZ_SCOPED_LOCK(new_channels_mutex);
        for (auto& it : new_channels)
        {
            channels.insert({it.first, std::move(it.second)});
        }
        new_channels.clear();
    }

    struct Iterator
    {
    private:
        decltype(ChannelGroup::channels)* channels;
        decltype(channels->begin()) it;

    public:
        Iterator(decltype(ChannelGroup::channels)* channels, decltype(channels->begin()) it) :
            channels(channels), it(it)
        {
        }

        typename decltype(ChannelGroup::channels)::iterator::reference operator *() { return *it; }

        typename decltype(ChannelGroup::channels)::iterator::pointer operator ->()
        {
            return it.operator ->();
        }

        Iterator& operator ++()
        {
            auto& channel = it->second;
            if (channel.is_closed())
            {
                channels->erase(it++);
            }
            else
            {
                ++it;
            }

            return *this;
        }

        bool operator ==(const Iterator& other) const { return it == other.it; }

        const uint64_t& first() const { return it->first; }

        Channel<TSend, TReceive>& second() { return it->second; }
    };

    Iterator begin() { return Iterator{&channels, channels.begin()}; }

    Iterator end() { return Iterator{&channels, channels.end()}; }

    Iterator find(uint64_t channel_id) { return Iterator{&channels, channels.find(channel_id)}; }
};

} // namespace hrz
