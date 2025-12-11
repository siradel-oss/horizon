#pragma once

#include "hrz/fnd/arena.h"
#include "hrz/fnd/class.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/function_ref.h"
#include "hrz/fnd/int128.h"

#include <optional>
#include <string>
#include <string_view>

extern "C"
{
    struct mu_Context;
}

namespace hrz
{
class HttpHeaders
{
public:
    struct OwnedHeader
    {
        std::string_view name;
        std::span<char> value;

        constexpr std::string_view value_str() const
        {
            return std::string_view{value.data(), value.size()};
        }
    };

private:
    Arena _arena{256};
    using Map = hrz::flat_hash_map<hrz::uint128, OwnedHeader>;

    // Key is murmur-hash of lower-case header name.
    // Value is name-value. The name is in the original case.
    // The string views point to the arena.
    // For convenience, they are zero-terminated. The null character is not in the view.
    Map _headers;

    mutable bool _dirty_hash = true;
    mutable uint64_t _hash_full = 0;
    mutable uint64_t _hash_content = 0;

    void refresh_hashes() const;

public:
    HttpHeaders() = default;
    ~HttpHeaders() = default;

    HttpHeaders(const HttpHeaders& other) : HttpHeaders() { *this = other; }

    HttpHeaders& operator=(const HttpHeaders&);

    HRZ_DEFAULT_MOVE(HttpHeaders);

    void swap(HttpHeaders& other);

    void set_header(std::string_view name, std::string_view value);
    std::string_view get_header(std::string_view name) const;
    void remove_header(std::string_view name);

    Map::const_iterator begin() const { return _headers.begin(); }

    Map::const_iterator end() const { return _headers.end(); }

    // Hash of the all the headers.
    uint64_t hash_full() const;

    // Hash of only the headers used for content negotiation.
    // Namely those are the Accept* headers.
    uint64_t hash_content() const;

    void clear();

    template<typename H>
    friend H AbslHashValue(H h, const HttpHeaders& headers)
    {
        return H::combine(std::move(h), headers.hash_full());
    }
};

// The callback is called for each value. Is has as argument the field value
// and and empty string if the field has no value, and the field name and value
// if it has both.
// For example the following header would generate those calls:
// "public, max-age=60,no-cache="Header1,Header2"" =>
// - ("public", "")
// - ("max-age", "60")
// - ("no-cache", "\"Header1,Header2\"")
// The boolean return value indicates if parsing should continue or not.
// The function stops when a parsing error is encountered.
void parse_http_header_value(
    std::string_view value,
    char separator,
    hrz::function_ref<bool(std::string_view, std::string_view)> callback);

class HttpTime
{
    // Unix timestamp
    int64_t _timestamp;

public:
    constexpr HttpTime() : _timestamp(-1) {}

    // From UTC Unix timestamp
    explicit constexpr HttpTime(int64_t t) : _timestamp(t) {}

    HRZ_DEFAULT_COPY_MOVE(HttpTime);
    ~HttpTime() = default;

    // Parses an IMF-fixdate and returns the corresponding UTC Unix timestamp.
    // https://www.rfc-editor.org/rfc/rfc9110.html#section-5.6.7-10
    static HttpTime from_imf_fixdate(std::string_view);

    static constexpr HttpTime invalid() { return HttpTime(-1); }

    constexpr int64_t unix_timestamp() const { return _timestamp; }

    constexpr bool is_valid() const { return _timestamp >= 0; }

    // The span must be at least 30 chars long.
    // It will be null-terminated.
    void write_imf_fixdate(std::span<char>) const;

    // Originates from a temporary buffer that must not be freed.
    const char* get_imf_fixdate() const;
};

// An HTTPClock returns the current HttpTime.
// This is generally not useful but it's used for testing purposes mainly.
// Use HttpDefaultClock when you want the normal behaviour (now is actually now).
class IHttpClock
{
public:
    IHttpClock() = default;
    HRZ_DEFAULT_COPY_MOVE(IHttpClock);
    virtual ~IHttpClock() = default;

    virtual HttpTime now() const = 0;
};

class HttpDefaultClock : public IHttpClock
{
public:
    HttpTime now() const override;
};

struct HttpTicket
{
    uint64_t o;

    constexpr bool operator==(HttpTicket other) const { return o == other.o; }

    template<typename H>
    friend H AbslHashValue(H h, HttpTicket t)
    {
        return H::combine(std::move(h), t.o);
    }
};

enum class HttpRequestStatus
{
    Loading,
    Loaded,
    Canceled,
    Error,
};

class IHttpLoader
{
public:
    struct ResponseMetadata
    {
        HttpTicket ticket;
        int code;
        HttpRequestStatus status;
        std::string content_type;
        HttpHeaders headers;
        size_t data_size;
    };

    virtual ~IHttpLoader() = default;

    virtual void cleanup() = 0;

    virtual bool start_request(
        HttpTicket ticket,
        std::string_view url,
        uint64_t range_start,
        uint64_t range_size,
        const HttpHeaders& headers) = 0;

    virtual void cancel_request(HttpTicket ticket) = 0;

    virtual void work() = 0;

    virtual std::optional<ResponseMetadata> dequeue_finished_request() = 0;

    // Schedules a copy of the downloaded data to `dst`.
    // The memory region at `dst` must remain valid until the
    // data is copied.
    // Returns true if the copy can start, i.e. the data is
    // downloaded, the data hasn't already been copied, and
    // the region has the right size.
    virtual bool copy_data(HttpTicket ticket, std::span<std::byte> dst) = 0;

    // Returns true if the downloaded data has been copied
    // to the memory region given in `copy_data()`.
    virtual bool is_data_copied(HttpTicket ticket) = 0;

    virtual void free_data(HttpTicket ticket) = 0;

    virtual void clear_cache() {}

    virtual void dev_ui(mu_Context*) {}
};

} // namespace hrz
