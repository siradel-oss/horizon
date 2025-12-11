#include "hrz/core/blob_image.h"

#include "hrz/core/render/resource_context.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/fnd/log.h"

namespace hrz
{
my::ResourceHandle to_gpu(
    const BlobImage& image,
    GpuResourceContext* rc,
    bool generate_mipmaps,
    bool allow_allocation_failure,
    const monitoring::ResourceOwner& resource_owner,
    std::initializer_list<std::pair<MetadataString, MetadataString>> metadata)
{
    if (!image.valid())
    {
        HRZ_LOG_WARNING("Cannot create a GPU image from a non valid blob image");
        return my::ResourceHandle::null();
    }

    auto blob_data = image.blob().get_data();

    hrz::InlinedVector<std::span<const std::byte>, 16> data_spans;
    const std::byte* data_ptr = blob_data.data();
    for (uint32_t level = 0; level < image.levels(); ++level)
    {
        auto level_size = image.level_size_in_bytes(level);
        data_spans.push_back({data_ptr, level_size});
        data_ptr += level_size;
    }

    my::TextureResource res;
    res.layout.type = my::TextureLayout::Type2D;
    res.layout.format = image.format();
    res.layout.width = image.width();
    res.layout.height = image.height();
    res.layout.depth = 1;
    res.layout.levels = image.levels();
    res.data = {data_spans.data(), data_spans.size()};
    res.generate_mipmaps = generate_mipmaps && image.levels() == 1;
    res.allow_allocation_failure = allow_allocation_failure;

    return rc->alloc(&res, resource_owner, std::move(metadata));
}
} // namespace hrz
