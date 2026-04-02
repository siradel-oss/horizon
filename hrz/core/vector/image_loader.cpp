#include "hrz/core/vector/image_loader.h"

#include "hrz/common/blob_allocator.h"
#include "hrz/common/blob_image.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/blob_image.h"
#include "hrz/core/image_decoder.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/gen_index_pool.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/http.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/static_string.h"

#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace hrz::vt
{
namespace image_loader
{

struct ImageReference
{
    std::string url;
    hrz::HttpHeaders headers;

    bool operator ==(const ImageReference& other) const
    {
        return url == other.url && headers.hash_content() == other.headers.hash_content();
    }

    template<typename H>
    friend H AbslHashValue(H h, const ImageReference& ref)
    {
        return H::combine(std::move(h), ref.url, ref.headers);
    }
};

struct Image
{
    enum class Status
    {
        New,
        Downloading,
        Decoding,
        Uploading,
        Loaded,
        Error,
    };

    Status status;
    uint32_t use_count;

    std::string url;
    hrz::HttpHeaders headers;
    hrz::assets_loader::Ticket load_ticket;
    hrz_jobs::DecodeBlobImageTicket decode_ticket;
    BlobImage image;
    lm::uvec2 size;
    my::ResourceHandle texture;
    monitoring::ResourceOwner resource_owner;
};

} // namespace image_loader

using namespace image_loader;

struct ImageLoader
{
    using ImageIndexPool = hrz::GenIndexPool<ImageH, 16, 16>;
    using ImagePool = hrz::GenObjectPool<Image, ImageIndexPool, 32>;

    ImagePool images;
    hrz::flat_hash_map<ImageReference, ImageH> image_refs_to_handles;

    hrz::flat_hash_set<ImageH> loading_images;

    std::vector<my::ResourceHandle> unused_textures;
};

namespace image_loader
{

ImageLoader* create_loader()
{
    return new ImageLoader();
}

void destroy_loader(ImageLoader* loader)
{
    assert(loader);
    delete loader;
}

ImageH load_image(
    ImageLoader* loader,
    std::string_view url_view,
    const hrz_proto::HttpHeaderList& headers,
    const monitoring::ResourceOwner& resource_owner)
{
    assert(loader);

    std::string url = {url_view.data(), url_view.size()};
    auto packed_headers = assets_loader::from_proto(headers);
    ImageReference image_ref = {url, std::move(packed_headers)};

    ImageH handle;

    auto it = loader->image_refs_to_handles.find(image_ref);
    if (it != loader->image_refs_to_handles.end())
    {
        auto image = loader->images.get_object(it->second);
        image->use_count += 1;
        handle = it->second;
    }
    else
    {
        handle = loader->images.alloc();
        auto image = loader->images.get_object(handle);
        image->status = Image::Status::New;
        image->url = url;
        image->headers = packed_headers;
        image->use_count = 1;
        image->load_ticket = 0;
        image->texture = my::ResourceHandle::null();
        image->resource_owner = resource_owner;

        loader->image_refs_to_handles.insert({image_ref, handle});
    }

    loader->loading_images.insert(handle);

    return handle;
}

void release_image(ImageLoader* loader, ImageH handle)
{
    assert(loader);

    auto image = loader->images.get_object(handle);

    if (image == nullptr)
    {
        return;
    }

    if (image->use_count == 0)
    {
        HRZ_LOG_WARNING("Image {} released too many times", handle);
        return;
    }

    image->use_count -= 1;

    if (image->use_count == 0)
    {
        loader->loading_images.insert(handle);
    }
}

bool is_image_valid(ImageLoader* loader, ImageH handle)
{
    assert(loader);

    return handle != 0 && loader->images.get_object(handle) != nullptr;
}

ImageStatus get_image_status(ImageLoader* loader, ImageH handle)
{
    assert(loader);

    auto image = loader->images.get_object(handle);

    if (image == nullptr)
    {
        HRZ_LOG_ERROR("Unknown image handle: {}", handle);
        return ImageStatus::Error;
    }

    switch (image->status)
    {
        case Image::Status::New:
        case Image::Status::Downloading:
        case Image::Status::Decoding:
        case Image::Status::Uploading: return ImageStatus::Loading;
        case Image::Status::Loaded: return ImageStatus::Loaded;
        case Image::Status::Error: return ImageStatus::Error;
        default: assert(false && "Unhandled case"); return ImageStatus::Error;
    }
}

Texture get_image_texture(ImageLoader* loader, ImageH handle)
{
    assert(loader);

    auto image = loader->images.get_object(handle);

    if (image == nullptr)
    {
        HRZ_LOG_ERROR("Unknown image handle: {}", handle);
        return {my::ResourceHandle::null(), {0, 0}};
    }

    if (image->status != Image::Status::Loaded)
    {
        HRZ_LOG_ERROR("Unknown image at \"{}\" isn't loaded", image->url);
        return {my::ResourceHandle::null(), {0, 0}};
    }

    return {image->texture, image->size};
}

void work(ImageLoader* loader, AssetsLoader* al, BlobAllocator* ba, JobScheduler* js)
{
    assert(loader && al && ba && js);

    for (auto it = loader->loading_images.begin(); it != loader->loading_images.end();)
    {
        ImageH handle = *it;
        Image* image = loader->images.get_object(handle);
        bool erase = false;

        if (image)
        {
            if (image->use_count == 0)
            {
                if (image->status == Image::Status::Downloading)
                {
                    assets_loader::end(al, image->load_ticket);
                }

                loader->unused_textures.push_back(image->texture);
                loader->image_refs_to_handles.erase({image->url, image->headers});
                loader->images.release(handle);
                erase = true;
            }
            else if (image->status == Image::Status::New)
            {
                if (!image->url.empty())
                {
                    image->load_ticket = assets_loader::begin(
                        al, image->url, image->headers, assets_loader::Queue::Default, 0,
                        {hrz::monitoring::systems::Symbols});
                    image->status = Image::Status::Downloading;
                }
                else
                {
                    // No URL has been provided.
                    image->status = Image::Status::Error;
                    erase = true;
                }
            }
            else if (image->status == Image::Status::Downloading)
            {
                if (assets_loader::is_valid(al, image->load_ticket)
                    && assets_loader::is_finished(al, image->load_ticket))
                {
                    auto status = assets_loader::get_status(al, image->load_ticket);

                    if (status == assets_loader::RequestStatus::Loaded)
                    {
                        auto content_type = assets_loader::get_content_type(al, image->load_ticket);
                        auto blob = assets_loader::get_blob(al, ba, image->load_ticket);
                        image->decode_ticket = image_decoder::decode_async(
                            js, std::move(blob), {hrz::monitoring::systems::Symbols},
                            hrz_proto::ImageFormat::SRGBA_8, content_type);

                        image->status = Image::Status::Decoding;
                    }
                    else if (status == assets_loader::RequestStatus::Error)
                    {
                        HRZ_LOG_ERROR("Couldn't load image at \"{}\"", image->url);
                        assets_loader::end(al, image->load_ticket);
                        image->status = Image::Status::Error;
                        erase = true;
                    }

                    assets_loader::end(al, image->load_ticket);
                }
            }
            else if (image->status == Image::Status::Decoding)
            {
                if (hrz_jobs::is_job_valid(js, image->decode_ticket)
                    && hrz_jobs::is_job_finished(js, image->decode_ticket))
                {
                    auto status = hrz_jobs::get_job_status(js, image->decode_ticket);

                    if (status == job_scheduler::JobStatus::Finished_Success)
                    {
                        auto blob_image = image_decoder::job_to_image(js, image->decode_ticket);

                        if (blob_image.valid())
                        {
                            image->image = std::move(blob_image);
                            image->size = {blob_image.width(), blob_image.height()};
                            image->status = Image::Status::Uploading;
                        }
                        else
                        {
                            HRZ_LOG_ERROR(
                                "Couldn't decode image at \"{}\": invalid image", image->url);
                            image->status = Image::Status::Error;
                            erase = true;
                        }
                    }
                    else
                    {
                        HRZ_LOG_ERROR("Couldn't decode image at \"{}\"", image->url);
                        image->status = Image::Status::Error;
                        erase = true;
                    }
                }
            }
        }
        else
        {
            erase = true;
        }

        if (erase)
        {
            loader->loading_images.erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

void work_gpu(ImageLoader* loader, Render* render)
{
    assert(loader && render);

    for (const auto& texture : loader->unused_textures)
    {
        render->rc->dealloc(texture);
    }
    loader->unused_textures.clear();

    for (auto it = loader->loading_images.begin(); it != loader->loading_images.end();)
    {
        ImageH handle = *it;
        Image* image = loader->images.get_object(handle);
        bool erase = false;

        if (image)
        {
            if (image->status == Image::Status::Uploading)
            {
                image->texture = hrz::to_gpu(
                    std::move(image->image), render->rc, true, true, image->resource_owner,
                    {{"URL"_ss, image->url}});
                image->status = Image::Status::Loaded;
            }
        }
        else
        {
            erase = true;
        }

        if (erase)
        {
            loader->loading_images.erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace image_loader
} // namespace hrz::vt
