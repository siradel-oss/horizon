#pragma once

#include "hrz_core_scene_model.h"
#include "hrz_core_scene_path.h"

#include <hrz_protocol_all.h>

namespace hrz::scene_model
{
// ArraySync is used to synchronize two arrays that might received progressive
// updated. There is an authoritative scene model array that receives
// progressive updates (add element, remove element, update element, rebuild
// all elements) at any time, and a mirror array that must later on catch up to
// all those updates by fetching new values from the authoritative array in the
// scene model that is now in a new state.
//
// To configure ArraySync for your use case, see the Traits documentation below,
// which is used as template parameter of ArraySync.
//
// To use this utility, all update notifications must be sent to the ArraySync
// instance using its "notify_model_update" method. Then, where you want to
// synchronize the auth and mirror arrays, call the "synchronize" method giving
// it a callback function that gives you all the update commands in order.

#if 0
struct Traits
{
    // This enum should contain the types of updates that can be handled by the
    // mirroring system. By default this should at least contain a value to
    // indicate that the whole element should be updated, but you can add more
    // update types. To have more granularity.
    enum ElementUpdateType
    {
        All,
        ...
    };

    // The path type of the message that contains the auth array.
    using ContainerPath = ...;

    // This path type of the element message.
    using ElementPath = ...;

    // Returns whether the path has an index for the array. Basically this must
    // call the "has_xxx_index" method where xxx is the auth array.
    static inline bool has_element_index(const ContainerPath& path)
    {
        return path.has_xxx_index();
    }

    // Returns the path index for the auth array path. Basically this must call
    // the "xxx_index" method where xxx is the auth array. It's implied that
    // the existence of this index will have been tested with
    // "has_element_index" before.
    static inline size_t element_index(const ContainerPath& path)
    {
        return path.xxx_index();
    }

    // Returns the element path when the path contains an index. This must be
    // "path.clone().xxx()" where xxx is the auth array. It's implied that the
    // existence of the index will have been tested with "has_element_index"
    // before calling this function.
    static inline ElementPath element_path(const ContainerPath& path)
    {
        return path.clone().xxx();
    }

    // Returns the update type for the given element path. This is used to get
    // more granularity than just "update everything". For instance you could
    // detect here that the update only touched the palette of a material, and
    // then in the synchronization step, the update type will be given with the
    // update command, and the mirroring code is free to do what best suits the
    // update type.
    static inline ElementUpdateType element_update_type(const ElementPath& path)
    {
        return ElementUpdateType::All;
    }
};
#endif

template<typename Traits>
class ArraySync
{
public:
    struct Command
    {
        enum Type
        {
            Add,
            Remove,
            Update,
            RebuildAll,
        };

        Type type;

        union
        {
            struct
            {
                size_t index_auth;
            } add;

            struct
            {
                size_t index_mirror;
            } remove;

            struct
            {
                typename Traits::ElementUpdateType update_type;
                size_t index_auth;
                size_t index_mirror;
            } update;
        } info;

        static Command rebuild_all()
        {
            Command cmd;
            cmd.type = RebuildAll;
            return cmd;
        }

        static Command add(size_t index)
        {
            Command cmd;
            cmd.type = Add;
            cmd.info.add.index_auth = index;
            return cmd;
        }

        static Command remove(size_t index)
        {
            Command cmd;
            cmd.type = Remove;
            cmd.info.remove.index_mirror = index;
            return cmd;
        }

        static Command update(size_t index, typename Traits::ElementUpdateType update_type)
        {
            Command cmd;
            cmd.type = Update;
            cmd.info.update.update_type = update_type;
            cmd.info.update.index_auth = index;
            cmd.info.update.index_mirror = index;
            return cmd;
        }
    };

private:
    bool _rebuild_all = false;
    std::vector<Command> _cmds;
    size_t _auth_element_count = 0;
    size_t _mirror_element_count = 0;

    void handle_remove(size_t index_auth_to_remove)
    {
        // Find the command that added the element we're removing now, if it exists.
        auto add_cmd_it = std::find_if(
            _cmds.rbegin(), _cmds.rend(),
            [&](const Command& cmd) {
                return cmd.type == Command::Add && cmd.info.add.index_auth == index_auth_to_remove;
            });

        bool has_add_cmd_in_cmds = add_cmd_it != _cmds.rend();

        // When an element is removed we need to:
        //   1. Remove the command that added it because the element
        //      won't be in the scene model at synchronization
        //   2. Remove the commands that update it for the same reasons.
        //   3. Decrement the index in the auth array of the commands that add
        //      elements whose index is after the index we removed, because the
        //      array is shifted after deletion.
        //   4. Decrement the index in the auth array of the commands that update
        //      elements whose index is after the index we removed, because the
        //      array is shifted after deletion. If the add command was removed, we
        //      also need to decrement the mirror index.
        bool stop = false;
        for (auto it = _cmds.rbegin(); !stop && it != _cmds.rend();)
        {
            switch (it->type)
            {
                case Command::Add:
                {
                    if (it->info.add.index_auth == index_auth_to_remove)
                    {
                        // Case 1
                        it = std::make_reverse_iterator(_cmds.erase(std::next(it).base()));
                        stop = true;
                    }
                    else if (it->info.add.index_auth > index_auth_to_remove)
                    {
                        // Case 3
                        it->info.add.index_auth -= 1;
                        ++it;
                    }
                    else
                    {
                        ++it;
                    }
                    break;
                }
                case Command::Update:
                {
                    if (it->info.update.index_auth == index_auth_to_remove)
                    {
                        // Case 2
                        it = std::make_reverse_iterator(_cmds.erase(std::next(it).base()));
                    }
                    else if (it->info.update.index_auth > index_auth_to_remove)
                    {
                        // Case 4
                        it->info.update.index_auth -= 1;

                        // The add command was removed so the element wasn't even in the mirror
                        // array which means even the mirror index must be decremented.
                        if (has_add_cmd_in_cmds)
                        {
                            it->info.update.index_mirror -= 1;
                        }

                        ++it;
                    }
                    else
                    {
                        ++it;
                    }
                    break;
                }
                case Command::Remove:
                {
                    // There is a supposedly important case of decrementing the
                    // index of remove commands whose index is after the index of a
                    // pair of add-remove commands for this stream of commands,
                    // however this cannot happen because the element would have
                    // been added and removed between the other add and remove
                    // commands, because we can only insert at the end of the
                    // array.
                    ++it;
                    break;
                }
                default:
                {
                    ++it;
                    break;
                }
            }
        }

        if (!has_add_cmd_in_cmds)
        {
            _cmds.push_back(Command::remove(index_auth_to_remove));
        }

        _auth_element_count -= 1;
    }

public:
    void notify_model_update_all() { _rebuild_all = true; }

    void notify_model_update(
        scene_model::UpdateType update_type,
        const typename Traits::ContainerPath& path)
    {
        if (_rebuild_all) return;

        if (!Traits::has_element_index(path))
        {
            if (update_type == scene_model::UpdateType::Add)
            {
                _cmds.push_back(Command::add(_auth_element_count));
                _auth_element_count += 1;
            }
            else
            {
                _rebuild_all = true;
            }
        }
        else
        {
            size_t index = Traits::element_index(path);
            typename Traits::ElementPath element_path = Traits::element_path(path);

            assert(index < _auth_element_count);

            if (element_path.leaf() && update_type == scene_model::UpdateType::Remove)
            {
                handle_remove(index);
            }
            else
            {
                typename Traits::ElementUpdateType element_update_type =
                    Traits::element_update_type(element_path);

                _cmds.push_back(Command::update(index, element_update_type));
            }
        }
    }

    // The callback must return the element count in the mirror array after the
    // command has been executed. This is used to ensure consistency.
    void synchronize(std::function<size_t(const Command&)> callback)
    {
        if (_rebuild_all)
        {
            _mirror_element_count = callback(Command::rebuild_all());
            _auth_element_count = _mirror_element_count;
        }
        else
        {
            for (const auto& cmd : _cmds)
            {
                size_t new_count = callback(cmd);

                switch (cmd.type)
                {
                    case Command::Add: assert(new_count == _mirror_element_count + 1); break;
                    case Command::Update: assert(new_count == _mirror_element_count); break;
                    case Command::Remove:
                        assert(_mirror_element_count > 0 && new_count == _mirror_element_count - 1);
                        break;
                    case Command::RebuildAll:
                        // Rebuild all is handled separately
                        assert(false && "We shouldn't be here");
                        break;
                }

                _mirror_element_count = new_count;
            }

            assert(_mirror_element_count == _auth_element_count);
        }

        _rebuild_all = false;
        _cmds.clear();
    }

    // Use this to indicate that you have manually synced the auth and mirror
    // arrays, and this the update commands can be discarded.
    void reset(size_t element_count)
    {
        _mirror_element_count = element_count;
        _auth_element_count = element_count;
        _rebuild_all = false;
        _cmds.clear();
    }
};

} // namespace hrz::scene_model
