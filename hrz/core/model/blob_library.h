#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/base_url.h"

#include <memory>
#include <optional>
#include <span>
#include <stdint.h>

namespace hrz::model
{
/**
 * The blob library is a common interface to access all resources of a model.
 * The resources can either be embedded or come from a URL. When they come from
 * a URL, the blob library is responsible for streaming the resource in and out
 * of memory as it's required by other systems.
 * It's a façade to the asset loader and the blob allocator.
 * Right now it's specific to the models system but it could be used elsewhere.
 */
class BlobLibrary
{
public:
    static std::unique_ptr<BlobLibrary> create(
        BaseUrl base_url,
        const HttpHeaders& headers,
        assets_loader::Queue load_queue);

    struct Handle
    {
        uint64_t o;

        constexpr bool operator==(const Handle& other) const = default;

        template<typename H>
        friend H AbslHashValue(H h, const Handle& k)
        {
            return H::combine(std::move(h), k.o);
        }
    };

    struct ConfigH
    {
        uint64_t o;

        constexpr bool operator==(const ConfigH& other) const = default;
    };

    enum Status
    {
        Unloaded,
        Loading,
        Loaded,
        Error,
    };

    virtual ~BlobLibrary() = default;

    virtual const BaseUrl& get_base_url() const = 0;

    // Registers a set of templated URLs in the blob library.
    // The first element of the pair is the name of the template, the second
    // one is the templated URL.
    // Internally those configs are deduplicated.
    virtual ConfigH register_config(
        std::span<const std::pair<std::string_view, std::string_view>> templates) = 0;

    // Add a blob from a URL. A blob-allocator blob can be given if the data
    // was already loaded. If the data is only a subspan of the resource pointed
    // to by the URL, the size of this subspan is determined by the blob-allocator
    // blob, and the offset by the offset parameter.
    // If no pre-loaded blob-allocator blob is given, the blob is assumed to be
    // a subspan of the resource pointed to by the URL, starting from the provided
    // offset, up to the end of the resource.
    virtual Handle add_blob_from_url(
        BlobAllocator* ba,
        std::string_view url,
        size_t offset,
        uint32_t load_priority = 0,
        std::optional<blobs::BlobHandle> blob = std::nullopt) = 0;

    // Add a blob from a templated URL parameters set.
    // This may refer to multiple blobs, it requires a template URL config
    // to point to the specific blob to download.
    virtual Handle add_templated_blob_from_parameters(
        std::string_view template_name,
        std::span<const std::pair<std::string_view, std::string_view>> params,
        uint32_t load_priority = 0) = 0;

    // Increments the ref count of a resource. It will begin loading if
    // necessary.
    virtual void acquire(Handle, ConfigH) = 0;

    // Decrements the ref count of a resource. If it reaches zero, it will be
    // added to the list of resources that can be streamed out.
    virtual void release(Handle, ConfigH) = 0;

    virtual std::string get_uri(Handle, ConfigH) const = 0;
    virtual Status get_status(Handle, ConfigH) const = 0;

    // Returns blob & MIME type.
    virtual std::pair<blobs::BlobHandle, std::string_view> get_blob(Handle, ConfigH) const = 0;

    virtual void work(AssetsLoader*, BlobAllocator*) = 0;
    virtual void destroy(AssetsLoader*) = 0;

    // Returns true if the headers change could result in content negotiation differing from the
    // previous headers.
    virtual bool update_http_headers(const HttpHeaders&) = 0;
};

} // namespace hrz::model
