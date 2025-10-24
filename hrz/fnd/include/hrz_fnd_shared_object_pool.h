#pragma once

#include <hrz_fnd_class.h>
#include <hrz_fnd_hash.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace hrz
{
// @Todo Free empty chunks.

template<
    typename T,
    uint32_t ChunkSize = 128> // Number of objects per allocated chunk
class HandleObjectPool
{
private:
    static_assert(ChunkSize > 0, "ChunkSize non-zero");

    using PrivateHandle = uint32_t;

public:
    struct Handle
    {
        PrivateHandle handle;

        uint32_t to_int() const { return handle; }

        constexpr bool operator==(const Handle& other) const = default;

        bool is_null() const { return handle == std::numeric_limits<PrivateHandle>::max(); }

        size_t hash() const { return std::hash<PrivateHandle>{}(handle); }
    };

    static constexpr Handle NULL_HANDLE = {std::numeric_limits<PrivateHandle>::max()};

private:
    struct Cell
    {
        union
        {
            uint32_t next_free;
            std::byte object[sizeof(T)];
        };

        bool occupied;
    };

    struct Chunk
    {
        std::byte* actual_ptr; // Memory allocation location
        Cell* view;            // Aligned version of the above pointer
    };

    uintptr_t cell_alignment;
    uintptr_t cell_size;
    std::vector<Chunk> chunks;
    PrivateHandle next_free;
    PrivateHandle next_uninitialized;
    size_t occupied_count;

    static inline void get_indices(
        PrivateHandle handle,
        PrivateHandle* chunk_index,
        PrivateHandle* index_in_chunk)
    {
        assert(chunk_index && index_in_chunk);

        *chunk_index = handle / ChunkSize;
        *index_in_chunk = handle % ChunkSize;
    }

    Cell* get_cell(PrivateHandle handle)
    {
        PrivateHandle chunk_index, index_in_chunk;
        get_indices(handle, &chunk_index, &index_in_chunk);

        if (chunk_index >= chunks.size()) return nullptr;

        return (Cell*)((std::byte*)chunks.at(chunk_index).view + cell_size * index_in_chunk);
    }

    const Cell* get_cell(PrivateHandle handle) const
    {
        PrivateHandle chunk_index, index_in_chunk;
        get_indices(handle, &chunk_index, &index_in_chunk);

        if (chunk_index >= chunks.size()) return nullptr;

        return (Cell*)((std::byte*)chunks.at(chunk_index).view + cell_size * index_in_chunk);
    }

public:
    HandleObjectPool() : next_free(0), next_uninitialized(0), occupied_count(0)
    {
        auto alignment = std::max(alignof(T), alignof(Cell));
        auto size = sizeof(Cell);
        if (size % alignment != 0)
        {
            size = size + alignment - (size % alignment);
        }
        cell_alignment = alignment;
        cell_size = size;
    }

    HRZ_DELETE_COPY_MOVE(HandleObjectPool);

    ~HandleObjectPool()
    {
        for (auto& chunk : chunks)
        {
            free(chunk.actual_ptr);
        }
    }

    T* get_object(Handle handle)
    {
        auto cell = get_cell(handle.handle);
        if (!cell || !cell->occupied) return nullptr;
        return (T*)(&cell->object);
    }

    const T* get_object(Handle handle) const
    {
        auto cell = get_cell(handle.handle);
        if (!cell || !cell->occupied) return nullptr;
        return (const T*)(&cell->object);
    }

    T& at(Handle handle)
    {
        auto object = get_object(handle);
        assert(object);
        return *object;
    }

    const T& at(Handle handle) const
    {
        auto object = get_object(handle);
        assert(object);
        return *object;
    }

    Handle alloc()
    {
        assert(next_free <= next_uninitialized);

        PrivateHandle chunk_index, index_in_chunk;
        get_indices(next_free, &chunk_index, &index_in_chunk);

        assert(chunk_index <= chunks.size());
        if (chunk_index == chunks.size())
        {
            assert(index_in_chunk == 0);

            Chunk chunk;
            chunk.actual_ptr = (std::byte*)malloc(cell_size * ChunkSize + cell_alignment - 1);

            // Alignment
            uintptr_t ptr_i = (uintptr_t)chunk.actual_ptr + cell_alignment - 1;
            ptr_i = (ptr_i / cell_alignment) * cell_alignment;

            assert(ptr_i % alignof(T) == 0);
            assert(ptr_i % alignof(Cell) == 0);

            chunk.view = (Cell*)ptr_i;
            chunks.push_back(chunk);

            next_uninitialized = (chunks.size() - 1) * ChunkSize;
        }

        auto cell = get_cell(next_free);
        assert(cell);

        if (next_free == next_uninitialized)
        {
            cell->occupied = false;
            cell->next_free = next_free + 1;
            next_uninitialized += 1;
        }

        assert(!cell->occupied);

        auto handle = next_free;
        next_free = cell->next_free;

        cell->occupied = true;
        new (cell->object) T();

        occupied_count += 1;

        return {handle};
    }

    void release(Handle handle)
    {
        auto cell = get_cell(handle.handle);
        if (!cell || !cell->occupied) return;

        std::destroy_at((T*)(&cell->object));
        cell->occupied = false;
        cell->next_free = next_free;
        next_free = handle.handle;

        occupied_count -= 1;
    }

    size_t size() const { return occupied_count; }

    bool empty() const { return size() == 0; }

    class iterator
    {
    public:
        typedef std::forward_iterator_tag iterator_category;

        iterator(HandleObjectPool* p, PrivateHandle h) : pool(p), handle(h)
        {
            while (handle < pool->next_uninitialized && !pool->get_cell(handle)->occupied)
            {
                handle += 1;
            }
        }

        iterator& operator++()
        {
            advance();
            return *this;
        }

        iterator operator++(int)
        {
            iterator it = *this;
            advance();
            return it;
        }

        std::pair<Handle, T&> operator*()
        {
            return std::pair<Handle, T&>{{handle}, *(pool->get_object({handle}))};
        }

        constexpr bool operator==(const iterator& it) const = default;

    private:
        void advance()
        {
            do
            {
                handle += 1;
            } while (handle < pool->next_uninitialized && !pool->get_cell(handle)->occupied);
        }

        HandleObjectPool* pool;
        PrivateHandle handle;
    };

    class const_iterator
    {
    public:
        typedef std::forward_iterator_tag iterator_category;

        const_iterator(HandleObjectPool* p, PrivateHandle h) : pool(p), handle(h)
        {
            while (handle < pool->next_uninitialized && !pool->get_cell(handle)->occupied)
            {
                handle += 1;
            }
        }

        const_iterator& operator++()
        {
            advance();
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator it = *this;
            advance();
            return it;
        }

        std::pair<Handle, const T&> operator*()
        {
            return std::pair<Handle, const T&>{{handle}, *(pool->get_object({handle}))};
        }

        constexpr bool operator==(const const_iterator& it) const = default;

    private:
        void advance()
        {
            do
            {
                handle += 1;
            } while (handle < pool->next_uninitialized && !pool->get_cell(handle)->occupied);
        }

        HandleObjectPool* pool;
        PrivateHandle handle;
    };

    iterator begin() { return iterator(this, 0); }

    iterator end() { return iterator(this, next_uninitialized); }

    const_iterator begin() const { return const_iterator(this); }

    const_iterator end() const { return const_iterator(this, next_uninitialized); }
};

template<typename T, uint32_t ChunkSize = 128>
class RefCountedObjectPool
{
private:
    struct Holder
    {
        T object;
        uint32_t ref_count = 0;
    };

    using ObjectPool = hrz::HandleObjectPool<Holder, ChunkSize>;
    ObjectPool pool;

    std::vector<typename ObjectPool::Handle> released_objects;

public:
    using Handle = typename ObjectPool::Handle;
    static constexpr Handle NULL_HANDLE = ObjectPool::NULL_HANDLE;

    Handle alloc() { return pool.alloc(); }

    Handle alloc_and_retain()
    {
        auto handle = pool.alloc();
        retain(handle);
        return handle;
    }

    void retain(Handle handle)
    {
        auto holder = pool.get_object(handle);
        if (holder)
        {
            holder->ref_count += 1;
        }
        else if (!handle.is_null())
        {
            HRZ_LOG_WARNING("`retain()` called on unknown handle: {}", handle.to_int());
        }
    }

    void release(Handle handle)
    {
        auto holder = pool.get_object(handle);
        if (holder)
        {
            assert(holder->ref_count > 0);
            holder->ref_count -= 1;

            if (holder->ref_count == 0)
            {
                released_objects.push_back(handle);
            }
        }
        else if (!handle.is_null())
        {
            HRZ_LOG_WARNING("`release()` called on unknown handle: {}", handle.to_int());
        }
    }

    T* get_object(Handle handle) { return &(pool.get_object(handle)->object); }

    const T* get_object(Handle handle) const { return &(pool.get_object(handle)->object); }

    T& at(Handle handle) { return pool.at(handle).object; }

    const T& at(Handle handle) const { return pool.at(handle).object; }

    size_t ref_count(Handle handle) const
    {
        auto holder = pool.get_object(handle);
        if (!holder) return 0;
        return (size_t)holder->ref_count;
    }

    size_t size() const { return pool.size(); }

    bool empty() const { return size() == 0; }

    void collect_garbage(std::function<void(Handle h, T&)> on_delete_callback)
    {
        // We cannot use an iterator here, as new objects can be added to
        // the released object array while the iteration is going on, and
        // iterators ignore this.
        for (size_t i = 0; i < released_objects.size(); ++i)
        {
            const auto& handle = released_objects[i];
            auto holder = pool.get_object(handle);
            if (holder && holder->ref_count == 0)
            {
                on_delete_callback(handle, holder->object);
                pool.release(handle);
            }
        }

        released_objects.clear();
    }

    void collect_garbage()
    {
        collect_garbage([](Handle, T&) {});
    }

    class iterator
    {
    private:
        using ObjectPoolIterator = typename ObjectPool::iterator;

    public:
        typedef std::forward_iterator_tag iterator_category;

        explicit iterator(ObjectPoolIterator pool_it) : pool_it(pool_it) {}

        iterator& operator++()
        {
            ++pool_it;
            return *this;
        }

        iterator operator++(int)
        {
            iterator it = *this;
            pool_it++;
            return it;
        }

        std::pair<Handle, T&> operator*()
        {
            auto it = *pool_it;
            return std::pair<Handle, T&>{it.first, it.second.object};
        }

        constexpr bool operator==(const iterator& it) const = default;

    private:
        ObjectPoolIterator pool_it;
    };

    class const_iterator
    {
    private:
        using ObjectPoolIterator = typename ObjectPool::const_iterator;

    public:
        typedef std::forward_iterator_tag iterator_category;

        explicit const_iterator(ObjectPoolIterator pool_it) : pool_it(pool_it) {}

        const_iterator& operator++()
        {
            ++pool_it;
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator it = *this;
            pool_it++;
            return it;
        }

        std::pair<Handle, const T&> operator*()
        {
            return std::make_pair(pool_it->first, pool_it->second.object);
        }

        constexpr bool operator==(const const_iterator& it) const = default;

    private:
        ObjectPoolIterator pool_it;
    };

    iterator begin() { return iterator(pool.begin()); }

    iterator end() { return iterator(pool.end()); }

    const_iterator begin() const { return const_iterator(pool.begin()); }

    const_iterator end() const { return const_iterator(pool.end()); }
};

template<typename T, uint32_t ChunkSize>
constexpr typename RefCountedObjectPool<T, ChunkSize>::Handle
    RefCountedObjectPool<T, ChunkSize>::NULL_HANDLE;

template<typename T, uint32_t ChunkSize = 128>
class SharedObjectPool
{
private:
    using ObjectPool = RefCountedObjectPool<T, ChunkSize>;
    using Handle = typename ObjectPool::Handle;

    ObjectPool pool;

    class RefBase
    {
    public:
        T& operator*() { return value(); }

        const T& operator*() const { return value(); }

        T* operator->()
        {
            assert(has_value());
            return pool->pool.get_object(handle);
        }

        const T* operator->() const
        {
            assert(has_value());
            return pool->pool.get_object(handle);
        }

        explicit operator bool() const noexcept { return has_value(); }

        bool has_value() const { return pool != nullptr && handle != ObjectPool::NULL_HANDLE; }

        T& value()
        {
            assert(has_value());
            return pool->pool.at(handle);
        }

        const T& value() const
        {
            assert(has_value());
            return pool->pool.at(handle);
        }

        constexpr bool operator==(const RefBase& ref) const = default;

        Handle get_handle() const { return handle; }

        size_t ref_count() const { return pool->pool.ref_count(handle); }

        struct Hasher
        {
            size_t operator()(const RefBase& ref) const
            {
                return hrz::hash_mix(std::hash<SharedObjectPool*>{}(ref.pool), ref.handle.hash());
            }
        };

    protected:
        RefBase() = default;

        RefBase(SharedObjectPool* pool, Handle handle) : pool(pool), handle(handle) {}

        SharedObjectPool* pool = nullptr;
        Handle handle = ObjectPool::NULL_HANDLE;
    };

public:
    class WeakRef final : public RefBase
    {
    public:
        friend class SharedObjectPool;
        using RefBase::RefBase;

        // Return true if the object this reference refers to is still alive.
        bool is_valid() const
        {
            return this->has_value() && this->pool->pool.get_object(this->handle) != nullptr;
        }
    };

    class Ref final : public RefBase
    {
    public:
        friend class SharedObjectPool;
        using RefBase::RefBase;

        Ref() : RefBase() {}

        Ref(const Ref& ref) : RefBase(ref.pool, ref.handle) { this->retain_value(); }

        Ref(const WeakRef& ref) : RefBase(ref.pool, ref.handle)
        {
            this->retain_value_from_weak_ref();
        }

        Ref(Ref&& ref) noexcept :
            RefBase(
                std::exchange(ref.pool, nullptr),
                std::exchange(ref.handle, ObjectPool::NULL_HANDLE))
        {
        }

        ~Ref() { this->release_value(); }

        Ref& operator=(const Ref& ref)
        {
            if (this != &ref)
            {
                this->release_value();
                this->pool = ref.pool;
                this->handle = ref.handle;
                this->retain_value();
            }
            return *this;
        }

        Ref& operator=(Ref&& ref) noexcept
        {
            if (this != &ref)
            {
                this->release_value();
                this->pool = std::exchange(ref.pool, nullptr);
                this->handle = std::exchange(ref.handle, ObjectPool::NULL_HANDLE);
            }
            return *this;
        }

        Ref& operator=(WeakRef&& ref) noexcept
        {
            this->release_value();
            this->pool = ref.pool;
            this->handle = ref.handle;
            this->retain_value_from_weak_ref();
            return *this;
        }

        void release()
        {
            this->release_value();
            this->handle = ObjectPool::NULL_HANDLE;
            this->pool = nullptr;
        }

        WeakRef make_weak_ref() const { return WeakRef(RefBase::pool, RefBase::handle); }

    private:
        Ref(SharedObjectPool* pool, Handle handle) : RefBase(pool, handle) { this->retain_value(); }

        void retain_value()
        {
            if (this->has_value())
            {
                this->pool->pool.retain(this->handle);
            }
        }

        void retain_value_from_weak_ref()
        {
            if (this->has_value())
            {
                // The weak reference may have been referring to
                // a value that is no longer in the object pool.

                if (this->pool->pool.get_object(this->handle) != nullptr)
                {
                    // The value still exists.
                    // We switched from a weak to a strong reference,
                    // so the ref count must be incremented.
                    this->retain_value();
                }
                else
                {
                    // The value no longer exists, the contents of
                    // the reference must be cleared to avoid sub-
                    // sequent uses.
                    this->pool = nullptr;
                    this->handle = ObjectPool::NULL_HANDLE;
                }
            }
        }

        void release_value()
        {
            if (this->has_value())
            {
                this->pool->pool.release(this->handle);
            }
        }
    };

    Ref alloc() { return Ref{this, pool.alloc()}; }

    size_t size() const { return pool.size(); }

    bool empty() const { return size() == 0; }

    void collect_garbage(std::function<void(T&)> on_delete_callback)
    {
        pool.collect_garbage([&](Handle, T& value) { on_delete_callback(value); });
    }

    void collect_garbage()
    {
        collect_garbage([](T&) {});
    }

    class iterator
    {
    private:
        using ObjectPoolIterator = typename ObjectPool::iterator;

    public:
        typedef std::forward_iterator_tag iterator_category;

        iterator(SharedObjectPool<T, ChunkSize>* shared_object_pool, ObjectPoolIterator pool_it) :
            shared_object_pool(shared_object_pool), pool_it(pool_it)
        {
        }

        iterator& operator++()
        {
            ++pool_it;
            return *this;
        }

        iterator operator++(int)
        {
            iterator it = *this;
            pool_it++;
            return it;
        }

        WeakRef operator*() { return WeakRef{shared_object_pool, (*pool_it).first}; }

        bool operator==(const iterator& it) const { return it.pool_it == pool_it; }

    private:
        SharedObjectPool<T, ChunkSize>* shared_object_pool;
        ObjectPoolIterator pool_it;
    };

    class const_iterator
    {
    private:
        using ObjectPoolIterator = typename ObjectPool::const_iterator;

    public:
        typedef std::forward_iterator_tag iterator_category;

        const_iterator(
            SharedObjectPool<T, ChunkSize>* shared_object_pool,
            ObjectPoolIterator pool_it) :
            shared_object_pool(shared_object_pool), pool_it(pool_it)
        {
        }

        const_iterator& operator++() const
        {
            ++pool_it;
            return *this;
        }

        const_iterator operator++(int) const
        {
            const_iterator it = *this;
            pool_it++;
            return it;
        }

        const WeakRef operator*() const { WeakRef{shared_object_pool, (*pool_it).first}; }

        bool operator==(const const_iterator& it) const { return it.pool_it == pool_it; }

    private:
        SharedObjectPool<T, ChunkSize>* shared_object_pool;
        ObjectPoolIterator pool_it;
    };

    iterator begin() { return iterator(this, pool.begin()); }

    iterator end() { return iterator(this, pool.end()); }

    const_iterator begin() const { return const_iterator(pool.begin()); }

    const_iterator end() const { return const_iterator(pool.end()); }
};
} // namespace hrz
