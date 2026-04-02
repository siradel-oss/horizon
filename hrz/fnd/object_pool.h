#pragma once

#include "hrz/fnd/arena.h"
#include "hrz/fnd/class.h"
#include "hrz/fnd/defines.h"

namespace hrz
{

// This is an arena-backed object pool.
// Unlike the generational object pool, there is no lifetime or ownership tests
// in release builds. So use this carefully. Although those checks exist in
// debug mode.
// All memory is freed when the pool goes out of scope.
// There is a debug check when object are not trivially destructible
// and they have not all been returned to the pool.
template<typename T>
class ObjectPool
{
    Arena _arena;

    struct Node
    {
        union Payload
        {
            // This cannot be just T because otherwise the union would not be trivially
            // destructible, which is a requirement of ObjectPool.
            // @Todo(C++26) This will be possible with trivial unions.
            alignas(T) std::byte value[sizeof(T)];

            // The linked list of free nodes
            Node* next;

        } payload;

#if HRZ_DEBUG
        // Random number identifying the pool.
        // Should be null when the node is not in use, and non-null otherwise.
        uint64_t pool_id;
#endif
    };

    static_assert(
        sizeof(typename Node::Payload) >= sizeof(T)
            && alignof(typename Node::Payload) >= alignof(T),
        "Node payload size and alignment");

    static_assert(
        sizeof(Node) >= sizeof(T) && alignof(Node) >= alignof(T) && offsetof(Node, payload) == 0
            && offsetof(typename Node::Payload, value) == 0,
        "Node size and alignment");

    Node* _free_head;
#if HRZ_DEBUG
    uint64_t _pool_id;
    size_t _in_use_count;
#endif

public:
    explicit ObjectPool(size_t object_per_allocation = 128) :
        _arena(object_per_allocation * sizeof(Node)), _free_head(nullptr)
    {
#if HRZ_DEBUG
        _pool_id = (uint64_t)this;
        _in_use_count = 0;
#endif
    }

#if HRZ_DEBUG
    ~ObjectPool() { assert(std::is_trivially_destructible_v<T> || _in_use_count == 0); }
#endif

    HRZ_DELETE_COPY_MOVE(ObjectPool);

    template<typename... Args>
    T* acquire(Args&&... args)
    {
        Node* node{};

        if (_free_head)
        {
            node = std::exchange(_free_head, _free_head->payload.next);
        }
        else
        {
            node = _arena.alloc<Node>();
        }

#if HRZ_DEBUG
        node->pool_id = _pool_id;
        _in_use_count += 1;
#endif

        // This effectively "restarts" the lifetime of the union, and allows us
        // to change its active member without triggering static analysis tools.
        // This is because placement new is treated as a read of the field, so we would
        // read "value" while "next" was active if the node was recycled.
        auto* payload = new (&node->payload) Node::Payload;
        return new (payload->value) T(std::forward<Args>(args)...);
    }

    void release(T* obj)
    {
        std::destroy_at(obj);

        Node* node = std::launder(reinterpret_cast<Node*>(obj));

#if HRZ_DEBUG
        assert(_in_use_count > 0 && node->pool_id == _pool_id);
        _in_use_count -= 1;
        node->pool_id = 0;
#endif

        node->payload.next = std::exchange(_free_head, node);
    }
};

} // namespace hrz
