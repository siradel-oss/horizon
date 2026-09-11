// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/model/resources/resource.h"

#include <mycelium/properties.h>

namespace
{

using namespace hrz::model;

constexpr BlobLibrary::ConfigH NullCfg = {0};

const ModelDescriptor::Accessor* _get_accessor(ModelDescriptor* descriptor, int accessor_id)
{
    if (accessor_id < 0 || (size_t)accessor_id >= descriptor->accessors.size())
    {
        return nullptr;
    }

    return &descriptor->accessors[(size_t)accessor_id];
}

const ModelDescriptor::BufferView* _get_buffer_view(
    const ModelDescriptor* descriptor,
    const ModelDescriptor::Accessor* accessor)
{
    if (accessor == nullptr || !accessor->buffer_view.has_value())
    {
        return nullptr;
    }

    const int buffer_view_id = accessor->buffer_view.value();
    if (buffer_view_id < 0 || (size_t)buffer_view_id >= descriptor->buffer_views.size())
    {
        return nullptr;
    }

    return &descriptor->buffer_views[(size_t)buffer_view_id];
}

const ModelDescriptor::Buffer* _get_buffer(
    const ModelDescriptor* descriptor,
    const ModelDescriptor::BufferView* buffer_view)
{
    if (buffer_view == nullptr)
    {
        return nullptr;
    }

    const int buffer_id = buffer_view->buffer;
    if (buffer_id < 0 || (size_t)buffer_id >= descriptor->buffers.size())
    {
        return nullptr;
    }

    return &descriptor->buffers[(size_t)buffer_id];
}

std::optional<BlobLibrary::Handle> _get_blob_handle(
    const ModelDescriptor* descriptor,
    const ModelDescriptor::Buffer* buffer)
{
    if (buffer == nullptr)
    {
        return std::nullopt;
    }

    return buffer->blob.has_value() ? buffer->blob : descriptor->embedded_resources;
}

bool _is_blob_library_blob_ready(BlobLibrary::Status status)
{
    switch (status)
    {
        case BlobLibrary::Status::Error:
        case BlobLibrary::Status::Loaded: return true;
        default: return false;
    }
}

struct AccessorDecoder
{
    AnimationResource::Accessor* accessor;
    hrz::blobs::BlobHandle blob;

    template<std::convertible_to<float> T, bool kNormalized>
    struct FloatConvertOp
    {
        using ResultType = float;

        constexpr float operator ()(T v) const
        {
            if constexpr (kNormalized && std::integral<T>)
            {
                constexpr auto max_val = static_cast<float>(std::numeric_limits<T>::max());
                if constexpr (std::signed_integral<T>)
                {
                    return std::max(static_cast<float>(v) / max_val, -1.0F);
                }
                else
                {
                    return static_cast<float>(v) / max_val;
                }
            }
            else
            {
                return static_cast<float>(v);
            }
        }
    };

    template<bool kNormalized, std::convertible_to<float> T>
    static constexpr float convert(T value)
    {
        return FloatConvertOp<T, kNormalized>{}(value);
    }

    template<bool kNormalized, std::convertible_to<float> T, int N>
    static constexpr lm::Vector<float, N> convert(const lm::Vector<T, N>& vec)
    {
        return lm::apply(
            std::make_integer_sequence<int, N>{}, FloatConvertOp<T, kNormalized>{}, vec);
    }

    template<typename VecType, typename T, int N, bool kNormalized>
    void apply_inner()
    {
        auto data = blob.get_data();
        const std::byte* src = data.data() + accessor->byte_offset;

        accessor->data.resize(accessor->count * N);
        auto* dst = accessor->data.data();

        if (std::same_as<T, float> && sizeof(VecType) == accessor->byte_stride)
        {
            std::memcpy(dst, src, accessor->count * sizeof(float) * N);
        }
        else
        {
            for (size_t i = 0; i < accessor->count; ++i)
            {
                VecType v;
                std::memcpy(&v, src, sizeof(VecType)); // Might be mis-aligned
                src += accessor->byte_stride;

                const auto floats = convert<kNormalized>(v);
                std::memcpy(dst, &floats, sizeof(float) * N);
                dst += N;
            }
        }
    }

    template<typename T, int N, bool kNormalized>
    void apply()
    {
        if constexpr (N == 1)
        {
            apply_inner<T, T, 1, kNormalized>();
        }
        else
        {
            apply_inner<lm::Vector<T, N>, T, N, kNormalized>();
        }
    }
};

} // anonymous namespace

namespace hrz::model
{

std::optional<AnimationResource> AnimationResource::acquire(
    int animation_id,
    BlobLibrary* bl,
    ModelDescriptor* descriptor,
    const monitoring::ResourceOwner&)
{
    if (animation_id < 0 || (size_t)animation_id >= descriptor->animations.size())
    {
        HRZ_LOG_ERROR("Invalid animation ID {}", animation_id);
        return std::nullopt;
    }

    AnimationResource resource;

    auto register_accessor = [&](int accessor_id) -> bool
    {
        if (auto it = resource.accessor_defs.find(accessor_id); it != resource.accessor_defs.end())
        {
            return true;
        }

        const auto* accessor = _get_accessor(descriptor, accessor_id);
        const auto* buffer_view = _get_buffer_view(descriptor, accessor);
        const auto* buffer = _get_buffer(descriptor, buffer_view);

        if (!buffer) return false;

        auto blob_handle = _get_blob_handle(descriptor, buffer);
        if (!blob_handle.has_value())
        {
            return false;
        }

        const size_t format_size = my::vertex_size(accessor->type);
        const size_t stride = buffer_view->byte_stride > 0 ? buffer_view->byte_stride : format_size;
        if (stride < format_size)
        {
            return false;
        }

        const size_t offset = buffer_view->byte_offset + accessor->byte_offset;
        if (const size_t required_size = offset + stride * (accessor->count - 1) + format_size;
            required_size > buffer->byte_length)
        {
            return false;
        }

        AnimationResource::Accessor accessor_def{
            .blob_handle = blob_handle.value(),
            .type = accessor->type,
            .byte_offset = offset,
            .byte_stride = stride,
            .count = accessor->count,
            .data = {},
        };

        resource.used_buffers.add(accessor_def.blob_handle);
        resource.accessor_defs[accessor_id] = std::move(accessor_def);

        return true;
    };

    const auto& desc_anim = descriptor->animations[(size_t)animation_id];
    for (const auto& sampler : desc_anim.samplers)
    {
        if (!register_accessor(sampler.timestamp_accessor))
        {
            HRZ_LOG_ERROR("Could not register accessor for animation sampler timestamps");
            return std::nullopt;
        }

        if (!register_accessor(sampler.value_accessor))
        {
            HRZ_LOG_ERROR("Could not register accessor for animation sampler values");
            return std::nullopt;
        }

        resource.samplers_defs.push_back(
            Sampler{
                .timestamp_accessor = sampler.timestamp_accessor,
                .value_accessor = sampler.value_accessor,
            });

        resource.animation.samplers.push_back(
            Animation::Sampler{
                .interpolation = sampler.interpolation,
                .timestamps = {},
                .values = {},
            });
    }

    for (const auto& channel : desc_anim.channels)
    {
        resource.animation.channels.push_back(
            Animation::Channel{
                .sampler = channel.sampler,
                .target_id = channel.target_node,
                .target_property = channel.target_property,
            });
    }

    resource.used_buffers.iterate_waiting_on(
        [bl](BlobLibrary::Handle handle)
        {
            bl->acquire(handle, NullCfg);
            return false;
        });

    return resource;
}

void AnimationResource::work(BlobLibrary* bl, BlobAllocator*, JobScheduler*, ImageDecoder*)
{
    if (status == ResourceStatus::Loading)
    {
        used_buffers.iterate_waiting_on(
            [bl](BlobLibrary::Handle handle)
            { return _is_blob_library_blob_ready(bl->get_status(handle, NullCfg)); });

        if (used_buffers.all_ready())
        {
            bool all_good = true;
            used_buffers.iterate_all(
                [bl, &all_good](BlobLibrary::Handle handle)
                {
                    if (bl->get_status(handle, NullCfg) == BlobLibrary::Status::Error)
                    {
                        all_good = false;
                    }
                });

            if (all_good)
            {
                if (!fetch_data(bl))
                {
                    HRZ_LOG_ERROR("Could not decode animation data");
                    status = ResourceStatus::Error;
                }

                build_animation();
                if (!animation.is_valid())
                {
                    HRZ_LOG_ERROR("Could not build animation");
                    status = ResourceStatus::Error;
                }
                else
                {
                    status = ResourceStatus::Ready;
                }

                expunge_tmp_data(bl);
            }
        }
    }
}

void AnimationResource::expunge_tmp_data(BlobLibrary* bl)
{
    used_buffers.iterate_all([bl](BlobLibrary::Handle handle) { bl->release(handle, NullCfg); });
    used_buffers = {};

    accessor_defs.clear();
    samplers_defs.clear();
}

bool AnimationResource::fetch_data(BlobLibrary* bl)
{
    for (auto& [accessor_id, accessor] : accessor_defs)
    {
        auto [blob, _] = bl->get_blob(accessor.blob_handle, NullCfg);
        if (!blob.is_valid())
        {
            HRZ_LOG_ERROR("Could not get blob for animation accessor data");
            return false;
        }

        if (const size_t expected_size = accessor.byte_offset
                + accessor.byte_stride * (accessor.count - 1) + my::vertex_size(accessor.type);
            expected_size > blob.data_size())
        {
            HRZ_LOG_ERROR("Animation accessor data size is smaller than expected");
            return false;
        }

        my::apply_vertex_format(accessor.type, AccessorDecoder{&accessor, std::move(blob)});
    }

    return true;
}

void AnimationResource::build_animation()
{
    for (size_t i = 0; i < samplers_defs.size(); ++i)
    {
        const auto& sampler_def = samplers_defs[i];
        auto& sampler = animation.samplers[i];

        sampler.timestamps = accessor_defs[sampler_def.timestamp_accessor].data;
        sampler.values = accessor_defs[sampler_def.value_accessor].data;
    }
}

void AnimationResource::work_gpu(BlobAllocator*, BlobLibrary*, Render*) {}

void AnimationResource::destroy(
    BlobLibrary* bl,
    BlobAllocator*,
    JobScheduler*,
    std::vector<my::ResourceHandle>&)
{
    expunge_tmp_data(bl);
}

} // namespace hrz::model
