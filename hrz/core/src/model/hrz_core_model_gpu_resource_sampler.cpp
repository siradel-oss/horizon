#include "model/hrz_core_model_gpu_resources.h"

namespace hrz::model
{
std::optional<GpuSamplerResource> GpuSamplerResource::acquire(
    SamplerWithMipmapUsage sampler,
    BlobLibrary* bl,
    ModelDescriptor* descriptor,
    const monitoring::ResourceOwner& owner)
{
    if (sampler.sampler_id >= 0 && (size_t)sampler.sampler_id < descriptor->samplers.size())
    {
        return GpuSamplerResource(
            descriptor->samplers[(size_t)sampler.sampler_id], sampler.use_mipmaps, owner);
    }
    return std::nullopt;
}

GpuSamplerResource::GpuSamplerResource(
    const ModelDescriptor::Sampler& desc,
    bool use_mipmaps,
    const monitoring::ResourceOwner& owner) :
    desc(desc),
    use_mipmaps(use_mipmaps),
    render_handle(my::ResourceHandle::null()),
    status(GpuResourceStatus::Loaded),
    owner(owner)
{
}

void GpuSamplerResource::work(BlobLibrary*, BlobAllocator*, JobScheduler*, ImageDecoder*) {}

void GpuSamplerResource::work_gpu(BlobAllocator*, BlobLibrary* bl, Render* render)
{
    if (status == GpuResourceStatus::Loaded)
    {
        my::SamplerResource res;
        res.sampler.wrap_x = desc.wrap_s;
        res.sampler.wrap_y = desc.wrap_t;
        res.sampler.is_shadow = false;
        res.sampler.min_filter = desc.min_filter;
        res.sampler.mag_filter = desc.mag_filter;

        if (desc.mipmap_min_filter.has_value() && use_mipmaps)
        {
            res.sampler.mipmap_filter = desc.mipmap_min_filter.value();
            res.use_mipmaps = true;
        }
        else
        {
            res.sampler.min_filter = desc.min_filter;
            res.use_mipmaps = false;
        }

        render_handle = render->rc->alloc(
            &res, owner, {{"blob library base URL"_ss, bl->get_base_url().base()}});

        status = GpuResourceStatus::Ready;
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
