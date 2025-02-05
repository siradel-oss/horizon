#include "model/hrz_core_model_blob_library.h"

#include <hrz_fnd_defines.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_gen_index_pool.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_intern_string.h>
#include <hrz_fnd_string_utils.h>
#include <hrz_fnd_time.h>
#include <hrz_fnd_url_utils.h>
#include <hrz_fnd_variant.h>

#include <fmt/args.h>

#include <algorithm>

namespace std
{
template<>
struct hash<hrz::model::BlobLibrary::Handle>
{
    inline size_t operator()(const hrz::model::BlobLibrary::Handle& k) const
    {
        return hash<uint64_t>()(k.o);
    }
};
} // namespace std

namespace hrz::model
{

struct EmbeddedBlob
{
    bool valid;
    blobs::BlobHandle blob;
};

struct StreamableBlob
{
    BlobLibrary::Status status;
    uint32_t ref_count;
    uint32_t load_priority;
    std::string url;
    size_t offset;
    size_t length;
    assets_loader::Ticket load_ticket;
    blobs::BlobHandle blob;
};

struct BlobToEmbed
{
    bool loading;
    uint32_t load_priority;
    std::string url;
    size_t offset;
    size_t length;
    assets_loader::Ticket load_ticket;
};

struct TemplatedBlob
{
    std::string template_name;
    fmt::dynamic_format_arg_store<fmt::format_context> params;
    uint32_t load_priority;
    // Map from config handle to blob handle
    hrz::flat_hash_map<uint64_t, BlobLibrary::Handle> blobs;
};

using Blob = std::variant<EmbeddedBlob, StreamableBlob, BlobToEmbed, TemplatedBlob>;

std::optional<blobs::BlobHandle> _retrieve_data_from_assets_loader(
    AssetsLoader* al,
    BlobAllocator* ba,
    assets_loader::Ticket ticket,
    size_t offset,
    size_t length)
{
    std::optional<blobs::BlobHandle> blob;

    if (assets_loader::get_status(al, ticket) == assets_loader::RequestStatus::Loaded)
    {
        auto full_blob = assets_loader::get_blob(al, ba, ticket);

        if (full_blob.data_size() >= offset + length)
        {
            if (length > 0)
            {
                blob = {blobs::make_sub_blob(ba, full_blob, offset, length)};
            }
            else
            {
                blob = {blobs::make_sub_blob(ba, full_blob, offset)};
            }
        }
        else
        {
            HRZ_LOG_ERROR("Blob too small");
        }
    }

    assets_loader::end(al, ticket);
    return blob;
}

struct Config
{
    hrz::flat_hash_map<std::string, std::string> templates;
};

class BlobLibraryImpl : public BlobLibrary
{
    static constexpr double TRY_STREAM_OUT_EVERY_MS = 1'000;
    static constexpr double STREAM_OUT_AFTER_MS = 4'000;

    using HandlePool = GenIndexPool<uint64_t, 24, 40>;
    using BlobPool = GenObjectPool<Blob, HandlePool>;

    BaseUrl _base_url;
    HttpHeaders _http_headers;
    assets_loader::Queue _load_queue;

    BlobPool _blobs;
    InternString _interner;

    // @Todo We could deduplicate by URL. This would act even more like a
    // cache. But it's unclear whether this is necessary for now since glTF
    // producers should already deduplicate their references to resources.

    hrz::flat_hash_set<uint64_t> _to_embed;

    hrz::flat_hash_set<uint64_t> _to_stream_in;
    hrz::flat_hash_set<uint64_t> _streaming_in;
    hrz::flat_hash_map<uint64_t, double> _to_stream_out_since_ms;

    hrz::flat_hash_set<uint64_t> _all_blobs;

    // Map from config identity to the actual config.
    hrz::flat_hash_map<uint64_t, Config> _configs;

    double _last_try_stream_out;

    blobs::BlobHandle _empty_blob = {};

public:
    BlobLibraryImpl(BaseUrl base_url, const HttpHeaders& headers, assets_loader::Queue load_queue) :
        _base_url(std::move(base_url)),
        _http_headers(headers),
        _load_queue(load_queue),
        _last_try_stream_out(hrz::now_frame_ms())
    {
    }

    ~BlobLibraryImpl() override = default;

    const BaseUrl& get_base_url() const override { return _base_url; }

    ConfigH register_config(
        gsl::span<const std::pair<std::string_view, std::string_view>> templates) override
    {
        uint64_t identity = hrz::hash_kv(templates);

        auto it = _configs.find(identity);
        if (it == _configs.end())
        {
            Config cfg;
            for (const auto& tpl : templates)
            {
                cfg.templates.insert(
                    std::make_pair(std::string(tpl.first), std::string(tpl.second)));
            }

            _configs[identity] = std::move(cfg);
        }

        return ConfigH{identity};
    }

    Handle add_blob_from_url(
        BlobAllocator* ba,
        std::string_view url,
        size_t offset,
        uint32_t load_priority,
        std::optional<blobs::BlobHandle> blob) override
    {
        assert((blob.has_value() && ba) || !blob.has_value());

        if (str::starts_with(url, "data:"))
        {
            if (blob.has_value())
            {
                uint64_t handle = _blobs.alloc(EmbeddedBlob{true, blob.value()});
                _all_blobs.insert(handle);
                return Handle{handle};
            }
            else
            {
                uint64_t handle =
                    _blobs.alloc(BlobToEmbed{false, load_priority, std::string(url), offset, 0, 0});
                _to_embed.insert(handle);
                _all_blobs.insert(handle);
                return Handle{handle};
            }
        }
        else
        {
            std::string full_url = _base_url.derive(url);

            if (blob.has_value())
            {
                size_t length = blob->data_size();
                uint64_t handle = _blobs.alloc(StreamableBlob{
                    Status::Loaded, 0, load_priority, std::move(full_url), offset, length, 0,
                    blob.value()});
                _all_blobs.insert(handle);
                _can_be_streamed_out(handle);
                return Handle{handle};
            }
            else
            {
                uint64_t handle = _blobs.alloc(StreamableBlob{
                    Status::Unloaded,
                    0,
                    load_priority,
                    std::move(full_url),
                    0,
                    0,
                    0,
                    {}});
                _all_blobs.insert(handle);
                return Handle{handle};
            }
        }
    }

    Handle add_templated_blob_from_parameters(
        std::string_view template_name,
        gsl::span<const std::pair<std::string_view, std::string_view>> params,
        uint32_t load_priority) override
    {
        fmt::dynamic_format_arg_store<fmt::format_context> params_copy;
        for (const auto& param : params)
        {
            // fmtlib doesn't copy the argument name so we need to copy it to
            // ensure it lives long enough. We intern it to deduplicate them.
            const char* intern_param_name = _interner.intern(param.first);
            params_copy.push_back(fmt::arg(intern_param_name, std::string(param.second)));
        }

        uint64_t handle = _blobs.alloc(
            TemplatedBlob{std::string(template_name), std::move(params_copy), load_priority});
        _all_blobs.insert(handle);
        return Handle{handle};
    }

    void _can_be_streamed_out(uint64_t handle)
    {
        assert(_to_stream_out_since_ms.count(handle) == 0);
        _to_stream_out_since_ms.insert(std::make_pair(handle, hrz::now_frame_ms()));
    }

    void _cannot_be_streamed_out(uint64_t handle) { _to_stream_out_since_ms.erase(handle); }

    void _stream_out(AssetsLoader* al, BlobAllocator* ba, uint64_t handle)
    {
        auto* blob = _blobs.get_object(handle);
        if (blob && std::holds_alternative<StreamableBlob>(*blob))
        {
            auto& streamable = std::get<StreamableBlob>(*blob);
            assert(streamable.ref_count == 0);

            switch (streamable.status)
            {
                case Status::Loading:
                {
                    _to_stream_in.erase(handle);
                    _streaming_in.erase(handle);
                    assets_loader::end(al, streamable.load_ticket);
                    streamable.status = Status::Unloaded;
                    break;
                }
                case Status::Loaded:
                {
                    streamable.blob.release();
                    streamable.status = Status::Unloaded;
                    break;
                }
                default: break;
            }
        }
    }

    std::string get_uri(Handle handle, ConfigH cfg_h) const override
    {
        const Blob* blob = _blobs.get_object(handle.o);
        if (!blob)
        {
            return "";
        }

        return std::visit(
            [this, cfg_h](const auto& arg) -> std::string
            {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, EmbeddedBlob> || std::is_same_v<T, BlobToEmbed>)
                {
                    return "data:...";
                }
                else if constexpr (std::is_same_v<T, StreamableBlob>)
                {
                    return fmt::format("{} [{}:{}]", arg.url, arg.offset, arg.offset + arg.length);
                }
                else if constexpr (std::is_same_v<T, TemplatedBlob>)
                {
                    auto it = arg.blobs.find(cfg_h.o);
                    if (it != arg.blobs.end())
                    {
                        return get_uri(it->second, cfg_h);
                    }
                    else
                    {
                        return "";
                    }
                }
            },
            *blob);

        return "";
    }

    Status get_status(Handle handle, ConfigH cfg_h) const override
    {
        const Blob* blob = _blobs.get_object(handle.o);
        if (!blob)
        {
            return Status::Error;
        }

        return std::visit(
            [this, cfg_h](const auto& arg) -> Status
            {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, EmbeddedBlob>)
                {
                    return arg.valid ? Status::Loaded : Status::Error;
                }
                else if constexpr (std::is_same_v<T, BlobToEmbed>)
                {
                    return Status::Loading;
                }
                else if constexpr (std::is_same_v<T, StreamableBlob>)
                {
                    return arg.status;
                }
                else if constexpr (std::is_same_v<T, TemplatedBlob>)
                {
                    auto it = arg.blobs.find(cfg_h.o);
                    if (it != arg.blobs.end())
                    {
                        return get_status(it->second, cfg_h);
                    }
                    else
                    {
                        return Status::Error;
                    }
                }
                else
                {
                    return Status::Error;
                }
            },
            *blob);
    }

    const blobs::BlobHandle& get_blob(Handle handle, ConfigH cfg_h) const override
    {
        const Blob* blob = _blobs.get_object(handle.o);
        if (blob)
        {
            return std::visit(
                [this, cfg_h](const auto& arg) -> const blobs::BlobHandle&
                {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, EmbeddedBlob>)
                    {
                        if (arg.valid)
                        {
                            return arg.blob;
                        }
                    }
                    else if constexpr (std::is_same_v<T, StreamableBlob>)
                    {
                        if (arg.status == Status::Loaded)
                        {
                            return arg.blob;
                        }
                    }
                    else if constexpr (std::is_same_v<T, TemplatedBlob>)
                    {
                        auto it = arg.blobs.find(cfg_h.o);
                        if (it != arg.blobs.end())
                        {
                            return get_blob(it->second, cfg_h);
                        }
                    }
                    return _empty_blob;
                },
                *blob);
        }

        return _empty_blob;
    }

    void work(AssetsLoader* al, BlobAllocator* ba) override
    {
        for (auto it = _to_embed.begin(); it != _to_embed.end();)
        {
            bool erase = false;
            Blob* blob = _blobs.get_object(*it);
            if (blob && std::holds_alternative<BlobToEmbed>(*blob))
            {
                auto& to_embed = std::get<BlobToEmbed>(*blob);
                if (!to_embed.loading)
                {
                    to_embed.load_ticket = assets_loader::begin(
                        al, to_embed.url, to_embed.offset, to_embed.length, _http_headers,
                        _load_queue, to_embed.load_priority, {monitoring::systems::Models});
                    to_embed.loading = true;
                }
                else
                {
                    if (assets_loader::is_finished(al, to_embed.load_ticket))
                    {
                        auto asset_blob = _retrieve_data_from_assets_loader(
                            al, ba, to_embed.load_ticket, 0, to_embed.length);

                        EmbeddedBlob embedded;
                        embedded.valid = asset_blob.has_value();
                        if (embedded.valid)
                        {
                            embedded.blob = asset_blob.value();
                        }

                        *blob = embedded;
                        erase = true;
                    }
                }
            }
            else
            {
                assert(!"This shouldn't happen");
                erase = true;
            }

            if (erase)
            {
                _to_embed.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        for (uint64_t handle : _to_stream_in)
        {
            Blob* blob = _blobs.get_object(handle);
            if (blob && std::holds_alternative<StreamableBlob>(*blob))
            {
                auto& streamable = std::get<StreamableBlob>(*blob);
                assert(streamable.status == Status::Loading);
                streamable.load_ticket = assets_loader::begin(
                    al, streamable.url, streamable.offset, streamable.length, _http_headers,
                    _load_queue, streamable.load_priority, {monitoring::systems::Models});
                _streaming_in.insert(handle);
            }
            else
            {
                assert(!"This shouldn't happen");
            }
        }
        _to_stream_in.clear();

        for (auto it = _streaming_in.begin(); it != _streaming_in.end();)
        {
            bool erase = false;
            Blob* blob = _blobs.get_object(*it);
            if (blob && std::holds_alternative<StreamableBlob>(*blob))
            {
                auto& streamable = std::get<StreamableBlob>(*blob);
                assert(streamable.status == Status::Loading);

                if (assets_loader::is_finished(al, streamable.load_ticket))
                {
                    auto blob = _retrieve_data_from_assets_loader(
                        al, ba, streamable.load_ticket, 0, streamable.length);

                    if (blob.has_value())
                    {
                        streamable.blob = blob.value();
                        streamable.status = Status::Loaded;
                    }
                    else
                    {
                        streamable.status = Status::Error;
                    }

                    erase = true;
                }
            }
            else
            {
                assert(!"This shouldn't happen");
                erase = true;
            }

            if (erase)
            {
                _streaming_in.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        double now_ms = hrz::now_frame_ms();
        if (now_ms - _last_try_stream_out > TRY_STREAM_OUT_EVERY_MS)
        {
            _last_try_stream_out = now_ms;

            for (auto it = _to_stream_out_since_ms.begin(); it != _to_stream_out_since_ms.end();)
            {
                if (now_ms - it->second > STREAM_OUT_AFTER_MS)
                {
                    _stream_out(al, ba, it->first);
                    _to_stream_out_since_ms.erase(it++);
                }
                else
                {
                    ++it;
                }
            }
        }
    }

    void _destroy(AssetsLoader* al, Blob* blob) const
    {
        std::visit(
            [al](auto& arg)
            {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, BlobToEmbed>)
                {
                    if (arg.loading)
                    {
                        assets_loader::end(al, arg.load_ticket);
                    }
                }
                else if constexpr (std::is_same_v<T, EmbeddedBlob>)
                {
                    if (arg.valid)
                    {
                        arg.blob.release();
                    }
                }
                else if constexpr (std::is_same_v<T, StreamableBlob>)
                {
                    switch (arg.status)
                    {
                        case Status::Loading: assets_loader::end(al, arg.load_ticket); break;
                        case Status::Loaded: arg.blob.release(); break;
                        default: break;
                    }
                }
                // Nothing to do for TemplatedBlob. Their child blobs are destroyed anyway because
                // they are in `_all_blobs`.
            },
            *blob);
    }

    void destroy(AssetsLoader* al) override
    {
        for (uint64_t h : _all_blobs)
        {
            Blob* blob = _blobs.get_object(h);
            assert(blob);

            _destroy(al, blob);

            _blobs.release(h);
        }

        _all_blobs.clear();
        _to_stream_in.clear();
        _streaming_in.clear();
        _to_embed.clear();
    }

    void acquire(Handle h, ConfigH cfg_h) override
    {
        auto* blob = _blobs.get_object(h.o);
        if (blob)
        {
            if (std::holds_alternative<StreamableBlob>(*blob))
            {
                auto& streamable = std::get<StreamableBlob>(*blob);
                if (streamable.ref_count++ == 0 && streamable.status == Status::Unloaded)
                {
                    _to_stream_in.insert(h.o);
                    streamable.status = Status::Loading;
                }
                _cannot_be_streamed_out(h.o);
                assert(streamable.status != Status::Unloaded);
            }
            else if (std::holds_alternative<TemplatedBlob>(*blob))
            {
                auto& tpl = std::get<TemplatedBlob>(*blob);
                std::optional<Handle> inner_handle;
                auto blob_it = tpl.blobs.find(cfg_h.o);
                if (blob_it != tpl.blobs.end())
                {
                    inner_handle = blob_it->second;
                }
                else
                {
                    auto cfg = _configs.find(cfg_h.o);
                    if (cfg != _configs.end())
                    {
                        auto it = cfg->second.templates.find(tpl.template_name);
                        if (it != cfg->second.templates.end())
                        {
                            std::string configured_url = fmt::vformat(it->second, tpl.params);
                            inner_handle = add_blob_from_url(
                                nullptr, configured_url, 0, tpl.load_priority, std::nullopt);
                            tpl.blobs.insert(std::make_pair(cfg_h.o, inner_handle.value()));
                        }
                    }
                }

                if (inner_handle.has_value())
                {
                    acquire(inner_handle.value(), cfg_h);
                }
            }
        }
    }

    void release(Handle h, ConfigH cfg_h) override
    {
        auto* blob = _blobs.get_object(h.o);
        if (blob)
        {
            if (std::holds_alternative<StreamableBlob>(*blob))
            {
                auto& streamable = std::get<StreamableBlob>(*blob);
                assert(streamable.ref_count > 0);
                streamable.ref_count--;
                if (streamable.ref_count == 0)
                {
                    _can_be_streamed_out(h.o);
                }
            }
            else if (std::holds_alternative<TemplatedBlob>(*blob))
            {
                const auto& tpl = std::get<TemplatedBlob>(*blob);
                auto blob_it = tpl.blobs.find(cfg_h.o);
                if (blob_it != tpl.blobs.end())
                {
                    release(blob_it->second, cfg_h);
                }
            }
        }
    }

    bool update_http_headers(const HttpHeaders& http_headers) override
    {
        auto old = std::exchange(_http_headers, http_headers);
        return old.hash_content() != _http_headers.hash_content();
    }
};

std::unique_ptr<BlobLibrary> BlobLibrary::create(
    BaseUrl base_url,
    const HttpHeaders& headers,
    assets_loader::Queue load_queue)
{
    return std::unique_ptr<BlobLibrary>(
        new BlobLibraryImpl(std::move(base_url), headers, load_queue));
}

} // namespace hrz::model
