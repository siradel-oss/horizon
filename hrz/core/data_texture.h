#pragma once

#include "hrz/core/render/context.h"

#include <lin_maths.h>

#include <optional>
#include <span>

namespace hrz
{

enum class DataTextureStatus
{
    Uninitialized,
    Stale,
    Uploaded,
    Error,
};

// This is a utility class to store and update data textures meant to store
// additional data. For instance they can be used to store the color of model
// instances, or the ID of features.
template<
    size_t TextureWidth,
    my::TextureFormat Format,
    typename ExternalType,
    typename InternalType = ExternalType,
    bool kRetainData = true
>
struct DataTexture
{
    monitoring::ResourceOwner owner;
    std::vector<std::pair<MetadataString, MetadataString>> metadata;

    std::vector<ExternalType> data;
    std::optional<my::ResourceHandle> resource;
    DataTextureStatus texture_status;

    static_assert(sizeof(ExternalType) >= sizeof(InternalType), "Internal type too large");
    static constexpr size_t WidthMultiplier = sizeof(ExternalType) / sizeof(InternalType);

    template<typename T>
    static constexpr lm::Vector<T, 2> _get_instance_data_texture_size(T element_count)
    {
        if (element_count == 0) return {0, 0};

        return {
            std::min(element_count, (T)TextureWidth),
            std::max((element_count - 1) / (T)TextureWidth + 1, (T)1)
        };
    }

    DataTexture(
        const monitoring::ResourceOwner& owner,
        std::initializer_list<std::pair<MetadataString, MetadataString>> metadata) :
        owner(owner), texture_status(DataTextureStatus::Uninitialized)
    {
        for (const auto& it : metadata)
        {
            this->metadata.push_back({it.first, it.second});
        }
    }

    void set(std::span<const ExternalType> new_data)
    {
        auto new_size = _get_instance_data_texture_size(new_data.size());

        assert(!resource.has_value() || data.size() == new_size.x * new_size.y);

        data.resize(new_size.x * new_size.y);
        std::copy_n(new_data.begin(), new_data.size(), data.begin());
        texture_status = DataTextureStatus::Stale;
    }

    void fill(size_t count, const ExternalType& value)
    {
        auto new_size = _get_instance_data_texture_size(count);

        assert(!resource.has_value() || data.size() == new_size.x * new_size.y);

        data.resize(new_size.x * new_size.y, value);
        texture_status = DataTextureStatus::Stale;
    }

    void update(Render* render)
    {
        if (texture_status == DataTextureStatus::Stale)
        {
            if (!resource.has_value())
            {
                static constexpr ExternalType default_upload_data[] = {ExternalType()};
                std::span<const std::byte> data_span = {};

                my::TextureResource res;
                res.layout.type = my::TextureLayout::Type2D;
                res.layout.format = Format;
                res.layout.depth = 1;
                res.layout.levels = 1;
                res.generate_mipmaps = false;
                res.allow_allocation_failure = true;

                if (data.size() == 0)
                {
                    res.layout.width = WidthMultiplier;
                    res.layout.height = 1;
                    data_span =
                        std::as_bytes(std::span<const ExternalType>{default_upload_data, 1});
                }
                else
                {
                    auto size = _get_instance_data_texture_size(data.size());
                    assert(data.size() == size.x * size.y);

                    res.layout.width = size.x * WidthMultiplier;
                    res.layout.height = size.y;
                    data_span = std::as_bytes(std::span<const ExternalType>(data));
                }

                res.data = {&data_span, 1};

                resource = render->rc->alloc(&res, owner, {metadata.data(), metadata.size()});
            }
            else
            {
                auto size = _get_instance_data_texture_size(data.size());
                assert(data.size() == size.x * size.y);

                render->my->update_texture(
                    resource.value(), Format, 0, 0, 0, 0, size.x * WidthMultiplier, size.y, 1,
                    std::as_bytes(std::span<const ExternalType>(data)));
            }

            if constexpr (!kRetainData)
            {
                data.clear();
                data.shrink_to_fit();
            }

            texture_status =
                resource->is_null() ? DataTextureStatus::Error : DataTextureStatus::Uploaded;
        }
    }

    std::span<ExternalType> resize_and_get_data(size_t size)
    {
        texture_status = DataTextureStatus::Stale;
        auto new_size = _get_instance_data_texture_size(size);
        data.resize(new_size.x * new_size.y);
        return data;
    }

    inline const ExternalType& data_at(size_t i) const
        requires kRetainData
    {
        return data.at(i);
    }

    void destroy(std::vector<my::ResourceHandle>& to_destroy)
    {
        if (resource.has_value())
        {
            to_destroy.push_back(resource.value());
        }
    }

    constexpr DataTextureStatus status() const { return texture_status; }

    constexpr my::ResourceHandle get_resource() const
    {
        return resource.value_or(my::ResourceHandle::null());
    }
};

} // namespace hrz
