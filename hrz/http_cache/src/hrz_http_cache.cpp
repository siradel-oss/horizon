#include "hrz_http_cache.h"

#include <hrz_fnd_char_utils.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_lru.h>
#include <hrz_fnd_mem.h>
#include <hrz_fnd_string_utils.h>

#include <optional>
#include <span>

extern "C"
{
#include <microui/microui.h>
}

// There is a major simplification done in this implementation:
// We only care about the "private cache" case. We don't do any logic related
// to public/shared caches.

namespace
{
int64_t parse_int(std::string_view str)
{
    str = hrz::str::trim_s(str);
    if (str.empty()) return 0;

    bool negative = false;
    int64_t value = 0;

    if (str[0] == '-')
    {
        negative = true;
        str = str.substr(1);
    }

    auto end = str.end();
    for (auto it = str.begin(); it != end && hrz::is_ascii_digit(*it); ++it)
    {
        value = value * 10 + (int64_t)(*it - '0');
    }

    return negative ? -value : value;
}

void set_header_int(hrz::HttpHeaders& headers, std::string_view name, int64_t value)
{
    char buffer[24];
    auto end = fmt::format_to_n(buffer, HRZ_ARRAY_COUNT(buffer), "{}", value);
    *end.out = '\0';
    headers.set_header(name, {buffer, end.size});
}

hrz::uint128 build_cache_key(
    std::string_view url,
    const hrz::HttpHeaders& request_headers,
    std::span<const std::string> vary_header_names,
    uint64_t range_start,
    uint64_t range_size)
{
    hrz::uint128 url_hash = hrz::murmur3_x64_128(url);
    uint64_t headers_hash = 0;

    if (vary_header_names.size() == 1 && vary_header_names[0] == "*")
    {
        headers_hash = request_headers.hash_full();
    }
    else if (!vary_header_names.empty())
    {
        hrz::HttpHeaders vary_headers;
        for (const auto& h : vary_header_names)
        {
            vary_headers.set_header(h, request_headers.get_header(h));
        }
        headers_hash = vary_headers.hash_full();
    }

    uint64_t data[] = {url_hash.high, url_hash.low, headers_hash, range_start, range_size};
    return hrz::murmur3_x64_128(std::as_bytes(std::span<const uint64_t>(data)));
}

struct ResourceAge
{
    hrz::HttpTime response_time;
    int64_t corrected_initial_age;
    int64_t lifetime;

    static ResourceAge make(
        hrz::HttpTime request_time,
        hrz::HttpTime response_time,
        int64_t response_age,
        hrz::HttpTime response_date,
        int64_t lifetime)
    {
        // https://httpwg.org/specs/rfc9111.html#rfc.section.4.2.3
        int64_t apparent_age =
            std::max<int64_t>(0, response_time.unix_timestamp() - response_date.unix_timestamp());
        int64_t response_delay = response_time.unix_timestamp() - request_time.unix_timestamp();
        int64_t corrected_age_value = response_age + response_delay;
        int64_t corrected_initial_age = std::max(apparent_age, corrected_age_value);
        return ResourceAge{response_time, corrected_initial_age, lifetime};
    }

    constexpr bool is_fresh(hrz::HttpTime now) const
    {
        return now.unix_timestamp()
            < response_time.unix_timestamp() - corrected_initial_age + lifetime;
    }

    constexpr int64_t current_age(hrz::HttpTime now) const
    {
        return now.unix_timestamp() - response_time.unix_timestamp() + corrected_initial_age;
    }
};

// This is a LRU cache: it caches values identified by keys and maintains a LRU
// list updated on any access of an entry. Each stored entry has an age which
// is used to check if it's stale or not, possibly removing it from the cache
// when it is.
// Entries cannot be externally mutated, this is because we can maintain
// internal stats about the stored data. For example the total amount of bytes
// the cache stores. These stats are updated when the cache is updated and can
// be used to limit its size.
template<typename K, typename V, typename Stat>
class LruCache
{
public:
    struct Entry
    {
        Entry* prev{};
        Entry* next{};

        ResourceAge age;
        K key;
        V value;

        template<typename... Args>
        Entry(ResourceAge age, K k, Args&&... args) :
            age(age), key(k), value(std::forward<Args>(args)...)
        {
        }
    };

private:
    hrz::LruList<Entry> _lru;
    hrz::flat_hash_map<K, Entry*> _cache;
    Stat _stat;

public:
    const Entry* get_if_fresh_or_remove(K key, hrz::HttpTime now)
    {
        auto it = _cache.find(key);
        if (it != _cache.end())
        {
            if (it->second->age.is_fresh(now))
            {
                _lru.touch(it->second);
                return it->second;
            }
            else
            {
                _stat.on_remove((const V&)it->second->value);
                _lru.remove(it->second);
                _cache.erase(it);
            }
        }
        return nullptr;
    }

    template<typename... Args>
    const Entry* emplace_new(K key, ResourceAge age, Args&&... args)
    {
        assert(_cache.find(key) == _cache.end());
        auto* entry = _lru.emplace_front(age, key, std::forward<Args>(args)...);
        _cache.insert(std::make_pair(key, entry));
        _stat.on_add((const V&)entry->value);
        assert(_cache.size() == _lru.size());
        return entry;
    }

    const Entry* get(K key)
    {
        auto it = _cache.find(key);
        if (it != _cache.end())
        {
            _lru.touch(it->second);
            return it->second;
        }
        else
        {
            return nullptr;
        }
    }

    bool contains(K key) const { return _cache.contains(key); }

    bool get_mutable(K key, std::function<void(Entry&)> callback)
    {
        auto it = _cache.find(key);
        if (it != _cache.end())
        {
            _stat.on_remove((const V&)it->second->value);
            callback(*it->second);
            _stat.on_add((const V&)it->second->value);
            _lru.touch(it->second);
            return true;
        }
        else
        {
            return false;
        }
    }

    void remove_oldest()
    {
        if (!_lru.empty())
        {
            auto back = _lru.back();
            _stat.on_remove(back->value);
            _cache.erase(back->key);
            _lru.pop_back();
        }
    }

    void remove(K key)
    {
        auto it = _cache.find(key);
        if (it != _cache.end())
        {
            auto* node = it->second;
            _stat.on_remove(node->value);
            _cache.erase(it);
            _lru.remove(node);
        }
    }

    constexpr const Stat& stat() const { return _stat; }

    constexpr size_t size() const { return _lru.size(); }

    void clear()
    {
        for (const auto& entry : _cache)
        {
            _lru.remove(entry.second);
        }

        assert(_lru.empty());
        _cache.clear();

        _stat.on_clear();
    }
};

class HttpCacheLoader : public hrz::IHttpLoader
{
    hrz::IHttpClock* _clock;
    std::unique_ptr<IHttpLoader> _inner;

    size_t _max_cache_size;
    size_t _max_vary_cache_entries;
    size_t _cache_miss = 0;
    size_t _cache_hit = 0;

    struct StoredResponse
    {
        ResponseMetadata metadata;
        std::optional<std::string> etag;
        std::optional<hrz::HttpTime> last_modified;
        std::vector<std::byte> data; // @Todo(http_cache) Store data in a more appropriate place
    };

    struct StoredResponseSizeStat
    {
        size_t total_size = 0;

        void on_add(const StoredResponse& r) { total_size += r.data.size(); }

        void on_remove(const StoredResponse& r)
        {
            assert(total_size >= r.data.size());
            total_size -= r.data.size();
        }

        void on_clear() { total_size = 0; }
    };

    // Key is the full cache key (URL + Vary headers)
    using StoredResponsesCache = LruCache<hrz::uint128, StoredResponse, StoredResponseSizeStat>;
    StoredResponsesCache _stored_responses;

    struct StoredVary
    {
        std::vector<std::string> header_names;
    };

    struct StoredVaryNoopStat
    {
        void on_add(const StoredVary& r) {}

        void on_remove(const StoredVary& r) {}

        void on_clear() {}
    };

    // Key is the hashed URL only.
    // Stores the list of Vary headers.
    LruCache<hrz::uint128, StoredVary, StoredVaryNoopStat> _stored_varies;

    struct Request
    {
        std::string url;
        uint64_t range_start;
        uint64_t range_size;
        hrz::HttpHeaders request_headers;
        hrz::HttpTime request_time;

        ResponseMetadata response;
        std::vector<std::byte> response_data;
        std::optional<std::span<std::byte>> data_dst;
    };

    hrz::flat_hash_map<hrz::HttpTicket, Request> _requests;
    hrz::flat_hash_set<hrz::HttpTicket> _finished_requests;

    // There can be requests that are evicted from the cache while they are
    // being validated. In this case, if we obtained a 304, we need to restart
    // them without validation so that we can receive the fresh data.
    hrz::flat_hash_set<hrz::HttpTicket> _to_restart;

public:
    HttpCacheLoader(hrz::IHttpClock* clock, std::unique_ptr<IHttpLoader> inner, size_t max_size) :
        _clock(clock),
        _inner(std::move(inner)),
        _max_cache_size(max_size),
        _max_vary_cache_entries(2000)
    {
    }

    ~HttpCacheLoader() override = default;

    void cleanup() override { _inner->cleanup(); }

    bool start_request(hrz::HttpTicket ticket, Request& request)
    {
        hrz::HttpTime now = _clock->now();
        request.request_time = now;
        request.data_dst = std::nullopt;

        // @Todo(http_cache) Correctly handle range requests.
        // For now the range is part of the cache key, but we're supposed
        // to correctly combine requested fragments, which we don't do for now.

        hrz::uint128 vary_key = build_cache_key(request.url, {}, {}, 0, 0);
        hrz::uint128 key;

        {
            auto* vary = _stored_varies.get_if_fresh_or_remove(vary_key, now);
            if (vary)
            {
                key = build_cache_key(
                    request.url, request.request_headers, vary->value.header_names,
                    request.range_start, request.range_size);
            }
            else
            {
                key = build_cache_key(request.url, {}, {}, request.range_start, request.range_size);
            }
        }

        bool validating = false;

        auto* response = _stored_responses.get(key);
        if (response)
        {
            if (response->age.is_fresh(now))
            {
                request.response = response->value.metadata;
                request.response.ticket = ticket;
                request.response_data = response->value.data;

                // https://httpwg.org/specs/rfc9111.html#rfc.section.4
                // "When a stored response is used to satisfy a request without
                // validation, a cache MUST generate an Age header field (Section
                // 5.1), replacing any present in the response with a value equal
                // to the stored response's current_age; see Section 4.2.3."
                set_header_int(request.response.headers, "Age", response->age.current_age(now));

                _finished_requests.insert(ticket);
                _cache_hit += 1;
                return true;
            }

            if (response->value.etag.has_value())
            {
                request.request_headers.set_header("If-None-Match", response->value.etag.value());
                validating = true;
            }

            if (response->value.last_modified.has_value())
            {
                request.request_headers.set_header(
                    "If-Modified-Since", response->value.last_modified.value().get_imf_fixdate());
                validating = true;
            }

            if (!validating)
            {
                _stored_responses.remove(key);
            }
        }

        return _inner->start_request(
            ticket, request.url, request.range_start, request.range_size, request.request_headers);
    }

    bool start_request(
        hrz::HttpTicket ticket,
        std::string_view url,
        uint64_t range_start,
        uint64_t range_size,
        const hrz::HttpHeaders& headers) override
    {
        Request request;
        request.url = std::string(url);
        request.range_start = range_start;
        request.range_size = range_size;
        request.request_headers = headers;

        if (start_request(ticket, request))
        {
            _requests.insert(std::make_pair(ticket, request));
            return true;
        }
        else
        {
            return false;
        }
    }

    void cancel_request(hrz::HttpTicket ticket) override
    {
        _requests.erase(ticket);
        _finished_requests.erase(ticket);
        _inner->cancel_request(ticket);
    }

    static std::vector<std::string> parse_vary_header(std::string_view header_value)
    {
        std::vector<std::string> values;
        hrz::parse_http_header_value(
            header_value, ',',
            [&](std::string_view key, std::string_view value) -> bool
            {
                if (value != "")
                {
                    return true;
                }
                else if (key == "*")
                {
                    values.clear();
                    values.emplace_back("*");
                    return false;
                }
                else
                {
                    values.emplace_back(key);
                    return true;
                }
            });
        return values;
    }

    std::span<const std::string> process_and_get_vary_headers(
        const Request& request,
        ResourceAge age)
    {
        hrz::uint128 vary_key = build_cache_key(request.url, {}, {}, 0, 0);
        auto* vary = _stored_varies.get_if_fresh_or_remove(vary_key, age.response_time);
        if (vary)
        {
            return vary->value.header_names;
        }

        if (!request.response.headers.get_header("vary").empty())
        {
            std::vector<std::string> vary =
                parse_vary_header(request.response.headers.get_header("vary"));

            if (!vary.empty())
            {
                while (_stored_varies.size() >= _max_vary_cache_entries)
                {
                    _stored_varies.remove_oldest();
                }
                assert(_stored_varies.size() < _max_vary_cache_entries);

                auto* vary_entry =
                    _stored_varies.emplace_new(vary_key, age, StoredVary{std::move(vary)});
                return vary_entry->value.header_names;
            }
        }
        return {};
    }

    struct CacheControl
    {
        bool can_store;
        ResourceAge resource_age;
        std::optional<std::string> etag;
        std::optional<hrz::HttpTime> last_modified;

        constexpr bool should_be_stored() const
        {
            return can_store && (resource_age.lifetime > 0 || etag || last_modified);
        }
    };

    CacheControl parse_cache_control(
        const hrz::HttpHeaders& headers,
        hrz::HttpTime request_time,
        hrz::HttpTime response_time)
    {
        std::optional<int64_t> max_age = std::nullopt;
        bool has_no_store = false;
        bool has_no_cache = false;

        // "Cache directives are identified by a token, to be compared
        // case-insensitively, and have an optional argument that can
        // use both token and quoted-string syntax. For the directives
        // defined below that define arguments, recipients ought to
        // accept both forms, even if a specific form is required for
        // generation."
        // https://httpwg.org/specs/rfc9111.html#rfc.section.5.2
        hrz::parse_http_header_value(
            headers.get_header("Cache-Control"), ',',
            [&](std::string_view field, std::string_view value)
            {
                if (hrz::str::iequals(field, "max-age"))
                {
                    // "This directive uses the token form of the
                    // argument syntax: e.g., 'max-age=5' not
                    // 'max-age="5"'. A sender MUST NOT generate the
                    // quoted-string form."
                    // https://httpwg.org/specs/rfc9111.html#rfc.section.5.2.2.1
                    int64_t max_age_parsed = parse_int(value);
                    if (max_age_parsed >= 0)
                    {
                        max_age = max_age_parsed;
                    }
                }
                else if (hrz::str::iequals(field, "no-store"))
                {
                    has_no_store = true;
                    return false;
                }
                else if (hrz::str::iequals(field, "no-cache"))
                {
                    has_no_cache = true;
                }
                return true;
            });

        if (has_no_cache)
        {
            max_age = 0; // Always revalidate
        }

        hrz::HttpTime response_date = hrz::HttpTime::from_imf_fixdate(headers.get_header("Date"));
        if (!response_date.is_valid())
        {
            response_date = response_time;
        }

        if (!max_age.has_value())
        {
            auto expires = hrz::HttpTime::from_imf_fixdate(headers.get_header("Expires"));
            if (expires.is_valid())
            {
                max_age = expires.unix_timestamp() - response_date.unix_timestamp();
            }
        }

        int64_t response_age = parse_int(headers.get_header("Age"));
        if (response_age < 0) response_age = 0;

        CacheControl cache_control{};
        cache_control.resource_age = ResourceAge::make(
            request_time, response_time, response_age, response_date, max_age.value_or(0));
        cache_control.can_store = !has_no_store;

        std::string_view etag = headers.get_header("ETag");
        if (!etag.empty())
        {
            cache_control.etag = std::string(etag);
        }

        auto last_modified = hrz::HttpTime::from_imf_fixdate(headers.get_header("Last-Modified"));
        if (last_modified.is_valid())
        {
            cache_control.last_modified = last_modified;
        }

        return cache_control;
    }

    void remove_unwanted_headers(hrz::HttpHeaders& headers)
    {
        static std::string_view to_remove[] = {
            // https://httpwg.org/specs/rfc9111.html#rfc.section.3.1
            "Proxy-Authenticate",  "Proxy-Authentication-Info",
            "Proxy-Authorization", "Proxy-Connection",
            "Keep-Alive",          "TE",
            "Transfer-Encoding",   "Upgrade",
            "Connection",
        };

        for (const auto& h : to_remove)
        {
            headers.remove_header(h);
        }
    }

    void remove_cache_headers(hrz::HttpHeaders& headers)
    {
        headers.remove_header("Age");
        headers.remove_header("If-None-Match");
        headers.remove_header("If-Modified-Since");
    }

    hrz::uint128 build_response_cache_key(const Request& request, ResourceAge resource_age)
    {
        std::span<const std::string> vary_headers =
            process_and_get_vary_headers(request, resource_age);

        // We don't implement the no-cache="Header1,Header2" form of no-cache
        // because it's a bit difficult, not widely used, and the unqualified
        // no-cache is a more general behaviour than the qualified one (we have
        // to revalidate the headers that are forbidden from being reused,
        // which is pretty much like revalidating the whole request.).
        // And the specs says:
        // "Note: The qualified form of the directive is often handled by
        // caches as if an unqualified no-cache directive was received; that
        // is, the special handling for the qualified form is not widely
        // implemented."
        //  -slerouzic, 2023-02-03

        return build_cache_key(
            request.url, request.request_headers, vary_headers, request.range_start,
            request.range_size);
    }

    void store_response(const Request& request, hrz::HttpTime response_time)
    {
        CacheControl cache_control =
            parse_cache_control(request.response.headers, request.request_time, response_time);

        if (!cache_control.should_be_stored())
        {
            return;
        }

        hrz::uint128 key = build_response_cache_key(request, cache_control.resource_age);

        // Remove old if present
        if (_stored_responses.get(key))
        {
            _stored_responses.remove(key);
        }

        // Make some room in the cache for the new response.
        size_t max_cache_size_to_fit_new_data = _max_cache_size - request.response_data.size();
        while (_stored_responses.stat().total_size > max_cache_size_to_fit_new_data)
        {
            _stored_responses.remove_oldest();
        }

        // Insert the new response
        StoredResponse response;
        response.data = request.response_data;
        response.metadata = request.response;
        response.etag = cache_control.etag;
        response.last_modified = cache_control.last_modified;
        remove_unwanted_headers(response.metadata.headers);

        assert(_stored_responses.stat().total_size + response.data.size() <= _max_cache_size);
        _stored_responses.emplace_new(key, cache_control.resource_age, std::move(response));
    }

    bool fetch_validated_and_update_stored_response(Request& request, hrz::HttpTime response_time)
    {
        // "The server generating a 304 response MUST generate any of the
        // following header fields that would have been sent in a 200 (OK)
        // response to the same request:
        //  - Content-Location, Date, ETag, and Vary
        //  - Cache-Control and Expires"
        // https://httpwg.org/specs/rfc9110.html#rfc.section.15.4.5
        CacheControl cache_control =
            parse_cache_control(request.response.headers, request.request_time, response_time);

        hrz::uint128 key = build_response_cache_key(request, cache_control.resource_age);

        if (!cache_control.should_be_stored() || !_stored_responses.contains(key))
        {
            _stored_responses.remove(key);
            return false;
        }

        bool was_present = _stored_responses.get_mutable(
            key,
            [this, &request, &cache_control, &response_time](StoredResponsesCache::Entry& stored)
            {
                for (const auto& header : request.response.headers)
                {
                    stored.value.metadata.headers.set_header(
                        header.second.name, header.second.value_str());
                }
                remove_unwanted_headers(stored.value.metadata.headers);
                stored.age = cache_control.resource_age;
                stored.value.etag = cache_control.etag;
                stored.value.last_modified = cache_control.last_modified;

                request.response_data = stored.value.data;
                request.response.code = stored.value.metadata.code;
                request.response.content_type = stored.value.metadata.content_type;
                request.response.headers = stored.value.metadata.headers;
                request.response.data_size = stored.value.metadata.data_size;
                request.response.status = stored.value.metadata.status;

                set_header_int(
                    request.response.headers, "Age", stored.age.current_age(response_time));
            });

        assert(was_present);
        return was_present;
    }

    void work() override
    {
        hrz::HttpTime now = _clock->now();

        while (!_to_restart.empty())
        {
            auto it = _to_restart.begin();

            auto request_it = _requests.find(*it);
            if (request_it == _requests.end())
            {
                _to_restart.erase(it++);
                continue;
            }

            if (start_request(*it, request_it->second))
            {
                _to_restart.erase(it++);
            }
            else
            {
                // Couldn't restart, the inner queue might be full.
                break;
            }
        }

        _inner->work();

        while (true)
        {
            auto finished_opt = _inner->dequeue_finished_request();
            if (!finished_opt) break;

            auto finished = std::move(finished_opt).value();

            auto it = _requests.find(finished.ticket);
            if (it == _requests.end()) continue;

            bool must_restart = false;
            auto& request = it->second;

            request.response = std::move(finished);
            if (request.response.code == 304)
            {
                if (fetch_validated_and_update_stored_response(request, now))
                {
                    _cache_hit += 1;
                }
                else
                {
                    must_restart = true;
                }
            }
            else if (request.response.code >= 200 && request.response.code < 300)
            {
                request.response_data.resize(request.response.data_size);
                _inner->copy_data(request.response.ticket, request.response_data);
                assert(_inner->is_data_copied(request.response.ticket));

                // Try to store response only if not too large.
                if (request.response.data_size <= _max_cache_size / 4)
                {
                    store_response(request, now);
                }

                _cache_miss += 1;
            }

            if (!must_restart)
            {
                _inner->free_data(request.response.ticket);
                _finished_requests.insert(request.response.ticket);
            }
            else
            {
                _inner->free_data(request.response.ticket);
                remove_cache_headers(request.request_headers);
                _to_restart.insert(request.response.ticket);
            }
        }
    }

    std::optional<ResponseMetadata> dequeue_finished_request() override
    {
        if (_finished_requests.empty()) return std::nullopt;

        auto it = _finished_requests.begin();
        auto ticket = *it;
        _finished_requests.erase(it);

        auto it2 = _requests.find(ticket);
        if (it2 == _requests.end()) return std::nullopt;

        return it2->second.response;
    }

    void dev_ui(mu_Context* ctx) override
    {
        if (mu_header(ctx, "HTTP cache"))
        {
            fmt::memory_buffer buffer;

            buffer.clear();
            fmt::format_to(
                std::back_inserter(buffer), "{} requests stored", _stored_responses.size());
            buffer.push_back(0);
            mu_text(ctx, buffer.data());

            buffer.clear();
            hrz::bytes_to_string(_stored_responses.stat().total_size, buffer, false, false);
            fmt::format_to(std::back_inserter(buffer), " stored");
            buffer.push_back(0);
            mu_text(ctx, buffer.data());

            if (_cache_hit + _cache_miss > 0)
            {
                buffer.clear();
                fmt::format_to(
                    std::back_inserter(buffer), "{} cache hits ({:.2}%)", _cache_hit,
                    (float)_cache_hit / (_cache_hit + _cache_miss) * 100.0f);
                buffer.push_back(0);
                mu_text(ctx, buffer.data());
            }

            if (mu_button(ctx, "Clear cache"))
            {
                clear_cache();
            }

            _inner->dev_ui(ctx);
        }
    }

    bool copy_data(hrz::HttpTicket ticket, std::span<std::byte> dst) override
    {
        auto it = _requests.find(ticket);
        if (it == _requests.end()) return false;
        if (dst.size_bytes() < it->second.response_data.size()) return false;
        if (it->second.data_dst.has_value()) return false;

        memcpy(dst.data(), it->second.response_data.data(), it->second.response_data.size());
        it->second.data_dst = {dst};
        return true;
    }

    bool is_data_copied(hrz::HttpTicket ticket) override
    {
        auto it = _requests.find(ticket);
        if (it == _requests.end()) return false;
        return it->second.data_dst.has_value();
    }

    void free_data(hrz::HttpTicket ticket) override
    {
        _requests.erase(ticket);
        _finished_requests.erase(ticket);
    }

    void clear_cache() override
    {
        _inner->clear_cache();

        _cache_hit = 0;
        _cache_miss = 0;
        _stored_responses.clear();
        _stored_varies.clear();
    }
};

} // namespace

std::unique_ptr<hrz::IHttpLoader> hrz::create_http_cache_loader(
    IHttpClock* clock,
    std::unique_ptr<IHttpLoader> inner,
    size_t max_size)
{
    return std::make_unique<HttpCacheLoader>(clock, std::move(inner), max_size);
}
