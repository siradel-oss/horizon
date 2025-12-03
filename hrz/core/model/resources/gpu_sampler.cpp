#include "hrz/core/model/resources/resources.h"

namespace hrz::model
{
std::optional<GpuSamplerResource> GpuSamplerResource::acquire(
    SamplerWithParams sampler,
    BlobLibrary* bl,
    ModelDescriptor* descriptor,
    const monitoring::ResourceOwner& owner)
{
    if (sampler.sampler_id >= 0 && (size_t)sampler.sampler_id < descriptor->samplers.size())
    {
        return GpuSamplerResource(
            descriptor->samplers[(size_t)sampler.sampler_id], sampler.can_use_linear_filtering,
            sampler.can_use_mipmaps, owner);
    }
    return std::nullopt;
}

GpuSamplerResource::GpuSamplerResource(
    const ModelDescriptor::Sampler& desc,
    bool can_use_linear_filtering,
    bool can_use_mipmaps,
    const monitoring::ResourceOwner& owner) :
    desc(desc),
    can_use_linear_filtering(can_use_linear_filtering),
    can_use_mipmaps(can_use_mipmaps),
    render_handle(my::ResourceHandle::null()),
    status(ResourceStatus::Loaded),
    owner(owner)
{
}

void GpuSamplerResource::work(BlobLibrary*, BlobAllocator*, JobScheduler*, ImageDecoder*) {}

void GpuSamplerResource::work_gpu(BlobAllocator*, BlobLibrary* bl, Render* render)
{
    if (status == ResourceStatus::Loaded)
    {
        my::SamplerResource res;
        res.sampler.wrap_x = desc.wrap_s;
        res.sampler.wrap_y = desc.wrap_t;
        res.sampler.is_shadow = false;

        res.sampler.min_filter =
            can_use_linear_filtering ? desc.min_filter : my::SamplerParams::Filter::Nearest;
        res.sampler.mag_filter =
            can_use_linear_filtering ? desc.mag_filter : my::SamplerParams::Filter::Nearest;

        if (desc.mipmap_min_filter.has_value() && can_use_mipmaps)
        {
            res.sampler.mipmap_filter = can_use_linear_filtering
                ? desc.mipmap_min_filter.value()
                : my::SamplerParams::Filter::Nearest;
            res.use_mipmaps = true;
        }
        else
        {
            res.sampler.mipmap_filter = my::SamplerParams::Filter::Nearest;
            res.use_mipmaps = false;
        }

        render_handle = render->rc->alloc(
            &res, owner, {{"blob library base URL"_ss, bl->get_base_url().base()}});

        status = ResourceStatus::Ready;
    }
}

void GpuSamplerResource::destroy(
    BlobLibrary*,
    BlobAllocator*,
    JobScheduler*,
    std::vector<my::ResourceHandle>& to_destroy)
{
    if (!render_handle.is_null())
    {
        to_destroy.push_back(render_handle);
    }
}

} // namespace hrz::model
