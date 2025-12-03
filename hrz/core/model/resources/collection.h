#pragma once

#include "hrz/common/monitoring_defs.h"
#include "hrz/core/model/resources/resource.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/gen_index_pool.h"
#include "hrz/fnd/gen_object_pool.h"

namespace hrz::model
{

class BlobLibrary;
struct ModelDescriptor;

/**
 * A resource collection stores all reference-counted resources of a given
 * type reference by a given key type. Essentially it acts as a cache for
 * all resources of a model.
 */
template<ResourceKey Key, Resource<Key> T>
class ResourcesCollection
{
    using ResourceId = uint32_t;

    struct RefCount
    {
        size_t rc;
        T value;
    };

    using ResourceIndexPool = GenIndexPool<ResourceId, 16, 16>;
    using ResourcePool = GenObjectPool<RefCount, ResourceIndexPool, 8>;

    monitoring::ResourceOwner _resource_owner;

    hrz::flat_hash_set<Key> _loading_resources;
    hrz::flat_hash_set<Key> _uploading_resources;
    hrz::flat_hash_set<Key> _to_destroy;

    ResourcePool _resources;
    hrz::flat_hash_map<Key, ResourceId> _resources_index;

    inline const RefCount* _get_inner(const Key& key) const
    {
        auto it = _resources_index.find(key);
        if (it != _resources_index.end())
        {
            const auto* ptr = _resources.get_object(it->second);
            assert(ptr);
            return ptr;
        }
        else
        {
            return nullptr;
        }
    }

    inline RefCount* _get_inner(const Key& key)
    {
        auto it = _resources_index.find(key);
        if (it != _resources_index.end())
        {
            auto* ptr = _resources.get_object(it->second);
            assert(ptr);
            return ptr;
        }
        else
        {
            return nullptr;
        }
    }

public:
    explicit ResourcesCollection(const monitoring::ResourceOwner& resource_owner) :
        _resource_owner(resource_owner)
    {
    }

    void acquire(const Key& key, BlobLibrary* bl, ModelDescriptor* descriptor)
    {
        auto* resource = _get_inner(key);
        if (!resource)
        {
            std::optional<T> res = T::acquire(key, bl, descriptor, _resource_owner);
            if (res.has_value())
            {
                auto id = _resources.alloc(RefCount{1, std::move(res.value())});
                auto* obj = _resources.get_object(id);

                _resources_index.insert(std::make_pair(key, id));

                switch (obj->value.get_status())
                {
                    case ResourceStatus::Loading:
                    {
                        _loading_resources.insert(key);
                        break;
                    }
                    case ResourceStatus::Loaded:
                    {
                        _uploading_resources.insert(key);
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }
        }
        else
        {
            resource->rc += 1;
            if (resource->rc == 1) // Ref count was 0, so remove from the resources to destroy now
            {
                assert(_to_destroy.count(key) > 0);
                _to_destroy.erase(key);
            }
        }
    }

    void release(const Key& key)
    {
        auto* resource = _get_inner(key);
        if (resource)
        {
            assert(resource->rc > 0);
            resource->rc -= 1;
            if (resource->rc == 0)
            {
                assert(_to_destroy.count(key) == 0);
                _to_destroy.insert(key);
            }
        }
    }

    T* get(const Key& key)
    {
        auto* res = _get_inner(key);
        if (res)
        {
            return &res->value;
        }
        else
        {
            return nullptr;
        }
    }

    const T* get(const Key& key) const
    {
        const auto* res = _get_inner(key);
        if (res)
        {
            return &res->value;
        }
        else
        {
            return nullptr;
        }
    }

    void work(
        BlobLibrary* bl,
        BlobAllocator* ba,
        JobScheduler* js,
        ImageDecoder* imgdec,
        std::vector<my::ResourceHandle>& to_destroy)
    {
        for (auto it = _loading_resources.begin(); it != _loading_resources.end();)
        {
            auto* resource = _get_inner(*it);
            assert(resource);
            resource->value.work(bl, ba, js, imgdec);

            switch (resource->value.get_status())
            {
                case ResourceStatus::Loaded:
                {
                    _uploading_resources.insert(*it);
                    _loading_resources.erase(it++);
                    break;
                }
                case ResourceStatus::Ready:
                case ResourceStatus::Error:
                {
                    _loading_resources.erase(it++);
                    break;
                }
                default:
                {
                    ++it;
                    break;
                }
            }
        }

        if (!_to_destroy.empty())
        {
            for (const Key& key : _to_destroy)
            {
                auto it = _resources_index.find(key);
                assert(it != _resources_index.end());
                auto id = it->second;
                auto* resource = _get_inner(key);
                resource->value.destroy(bl, ba, js, to_destroy);

                _loading_resources.erase(key);
                _uploading_resources.erase(key);

                _resources.release(id);
                _resources_index.erase(key);
            }
            _to_destroy.clear();
        }
    }

    void work_gpu(BlobAllocator* ba, BlobLibrary* bl, Render* render)
    {
        for (auto it = _uploading_resources.begin(); it != _uploading_resources.end();)
        {
            auto* resource = _get_inner(*it);
            assert(resource);

            resource->value.work_gpu(ba, bl, render);

            switch (resource->value.get_status())
            {
                case ResourceStatus::Ready:
                case ResourceStatus::Error:
                {
                    _uploading_resources.erase(it++);
                    break;
                }
                default:
                {
                    ++it;
                    break;
                }
            }
        }
    }

    void destroy_all(
        BlobLibrary* bl,
        BlobAllocator* ba,
        JobScheduler* js,
        std::vector<my::ResourceHandle>& to_destroy)
    {
        for (auto& it : _resources_index)
        {
            auto* resource = _resources.get_object(it.second);
            assert(resource);
            resource->value.destroy(bl, ba, js, to_destroy);
            _resources.release(it.second);
        }

        _resources_index.clear();
        _loading_resources.clear();
        _uploading_resources.clear();
        _to_destroy.clear();
    }

    ResourceStatus get_status(const Key& key) const
    {
        const auto* resource = _get_inner(key);
        if (resource)
        {
            return resource->value.get_status();
        }
        else
        {
            return ResourceStatus::Error;
        }
    }
};

} // namespace hrz::model
