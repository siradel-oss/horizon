#pragma once

#include "hrz/fnd/class.h"
#include "hrz/fnd/linked_list.h"
#include "hrz/fnd/object_pool.h"

namespace hrz
{
// This is a doubly-linked list with LRU semantics.
// This means that we can "touch" elements to place them at the front of the list.
// We can also pop elements from the back.
// front/head is most recent
// back/tail is oldest
// Nodes should have the same layout as described in LinkedList<T>.
template<typename Node>
class LruList
{
    ObjectPool<Node> _pool;
    LinkedList<Node> _list;
    size_t _size = 0;

public:
    explicit LruList(size_t object_per_allocation = 128) : _pool(object_per_allocation) {}

    ~LruList()
    {
        Node* node = _list.head();
        while (_list.is_valid(node))
        {
            // We don't bother detaching because we're deleting everything anyway.
            _pool.release(std::exchange(node, node->next));
        }
    }

    HRZ_DELETE_COPY_MOVE(LruList);

    template<typename... Args>
    Node* emplace_front(Args&&... args)
    {
        Node* node = _pool.acquire(std::forward<Args>(args)...);
        _list.insert_head(node);
        _size += 1;
        return node;
    }

    // Removes the last (back) node
    // Returns true if the list was not empty, false otherwise
    bool pop_back()
    {
        if (empty()) return false;
        remove(_list.tail());
        return true;
    }

    // Places the node on the front of the list
    void touch(Node* node)
    {
        _list.detach(node);
        _list.insert_head(node);
    }

    void move_to_back(Node* node)
    {
        _list.detach(node);
        _list.insert_tail(node);
    }

    // Removes a node and destroy its content
    void remove(Node* node)
    {
        assert(!empty());
        _list.detach(node);
        _pool.release(node);
        _size -= 1;
    }

    constexpr size_t size() const { return _size; }

    constexpr bool empty() const { return _size == 0; }

    constexpr Node* front()
    {
        if (!empty())
        {
            return _list.head();
        }
        else
        {
            return nullptr;
        }
    }

    constexpr const Node* front() const
    {
        if (!empty())
        {
            return _list.head();
        }
        else
        {
            return nullptr;
        }
    }

    constexpr Node* back()
    {
        if (!empty())
        {
            return _list.tail();
        }
        else
        {
            return nullptr;
        }
    }

    constexpr const Node* back() const
    {
        if (!empty())
        {
            return _list.tail();
        }
        else
        {
            return nullptr;
        }
    }
};

} // namespace hrz
