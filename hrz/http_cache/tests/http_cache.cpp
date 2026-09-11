// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/http_cache/http_cache.h"

#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/http.h"
#include "hrz/fnd/string_utils.h"

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <optional>
#include <span>

using namespace hrz;

static constexpr size_t DefaultCacheSize = 1 * 1024 * 1024;

struct Response
{
    int code;
    HttpHeaders headers;
    std::string data;
    std::optional<std::span<std::byte>> data_dst;
};

struct RequestsStats
{
    int count = 0;
    int count_200 = 0;
    int count_206 = 0;
    int count_304 = 0;
};

class DummyServer : public IHttpLoader
{
    RequestsStats* _stats;
    IHttpClock* _clock;
    int64_t _epoch;
    flat_hash_map<HttpTicket, Response> _responses;
    flat_hash_set<HttpTicket> _finished;

public:
    DummyServer(IHttpClock* clock, RequestsStats* stats) :
        _stats(stats), _clock(clock), _epoch(_clock->now().unix_timestamp())
    {
    }

    void cleanup() override {}

private:
    void handle_request(std::string_view path, const HttpHeaders& headers, Response& response)
    {
        int64_t now = _clock->now().unix_timestamp();

        if (path == "/hello")
        {
            response.code = 200;
            response.data = "Hello, world!";
        }
        else if (path == "/counter")
        {
            response.code = 200;
            response.data = fmt::format("{}", now - _epoch);
        }
        else if (path == "/counter_max_age")
        {
            response.code = 200;
            response.data = fmt::format("{}", now - _epoch);
            response.headers.set_header("Cache-Control", "max-age=60");
        }
        else if (path == "/counter_expires")
        {
            response.code = 200;
            response.data = fmt::format("{}", now - _epoch);
            response.headers.set_header("Expires", HttpTime(now + 60).get_imf_fixdate());
        }
        else if (path == "/counter_no_store")
        {
            response.code = 200;
            response.data = fmt::format("{}", now - _epoch);
            response.headers.set_header("Cache-Control", "max-age=60,no-store");
        }
        else if (path == "/counter_no_cache")
        {
            response.code = 200;
            response.data = fmt::format("{}", now - _epoch);
            response.headers.set_header("Cache-Control", "max-age=60,no-cache");
        }
        else if (path == "/counter_vary")
        {
            auto user_name = headers.get_header("User-Name");
            response.code = 200;
            response.data = fmt::format("Hello {}, your counter is {}", user_name, now - _epoch);
            response.headers.set_header("Cache-Control", "max-age=60");
            response.headers.set_header("Vary", "User-Name,Cheese");
        }
        else if (path == "/uncached_headers")
        {
            response.code = 200;
            response.data = "Hello";
            response.headers.set_header("Cache-Control", "max-age=60");
            response.headers.set_header("Upgrade", "Yes-Please");
        }
        else if (path == "/cached_range")
        {
            response.code = 200;
            response.data = fmt::format("0123456789 - {} - Bye", now - _epoch);
            response.headers.set_header("Cache-Control", "max-age=60");
            response.headers.set_header("Upgrade", "Yes-Please");
        }
        else if (path == "/static_etag")
        {
            if (headers.get_header("If-None-Match") == "\"static_etag\"")
            {
                response.code = 304;
            }
            else
            {
                response.code = 200;
                response.data = "This is static";
            }

            response.headers.set_header("Cache-Control", "max-age=60");
            response.headers.set_header("ETag", "\"static_etag\"");
            response.headers.set_header("Date", HttpTime(_epoch).get_imf_fixdate());
        }
        else if (path == "/static_etag_changing_date")
        {
            if (headers.get_header("If-None-Match") == "\"static_etag\"")
            {
                response.code = 304;
            }
            else
            {
                response.code = 200;
                response.data = "This is static";
            }

            response.headers.set_header("Cache-Control", "max-age=60");
            response.headers.set_header("ETag", "\"static_etag\"");
        }
        else if (path == "/dynamic_etag")
        {
            // Document changes every minute.
            int64_t version = (_clock->now().unix_timestamp() - _epoch) / 60;
            std::string current_etag = fmt::format("\"Version {}\"", version);

            if (headers.get_header("If-None-Match") == current_etag)
            {
                response.code = 304;
            }
            else
            {
                response.code = 200;
                response.data = fmt::format("Version {}", version);
            }

            response.headers.set_header("Cache-Control", "no-cache");
            response.headers.set_header("ETag", current_etag);
        }
        else if (path == "/last_modified")
        {
            // Document changes every minute.
            int64_t version = (_clock->now().unix_timestamp() - _epoch) / 60;
            hrz::HttpTime current_version_date = hrz::HttpTime(_epoch + version * 60);

            hrz::HttpTime if_modified_since =
                hrz::HttpTime::from_imf_fixdate(headers.get_header("If-Modified-Since"));

            if (if_modified_since.is_valid()
                && if_modified_since.unix_timestamp() >= current_version_date.unix_timestamp())
            {
                response.code = 304;
            }
            else
            {
                response.code = 200;
                response.data = fmt::format("Version {}", version);
            }

            response.headers.set_header("Cache-Control", "no-cache");
            response.headers.set_header("Date", current_version_date.get_imf_fixdate());
            response.headers.set_header("Last-Modified", current_version_date.get_imf_fixdate());
        }
    }

public:
    bool start_request(
        HttpTicket ticket,
        std::string_view url,
        uint64_t range_start,
        uint64_t range_size,
        const HttpHeaders& headers) override
    {
        Response response;
        response.code = 404;
        response.headers.set_header("Date", _clock->now().get_imf_fixdate());
        response.data_dst = std::nullopt;

        if (url.starts_with("https://horizon.tests/"))
        {
            handle_request(url.substr(21), headers, response);
        }

        if (!response.data.empty())
        {
            response.headers.set_header("Content-Type", "text/plain");
            response.headers.set_header("Content-Length", fmt::format("{}", response.data.size()));

            // It's a suboptimal implementation but this is just for tests
            if (response.code == 200 && (range_start > 0 || range_size > 0)
                && range_start + range_size <= response.data.size())
            {
                response.code = 206;
                response.headers.set_header(
                    "Content-Range",
                    fmt::format(
                        "bytes {}-{}/{}", range_start, range_start + range_size - 1,
                        response.data.size()));
                response.data =
                    std::string(std::string_view(response.data).substr(range_start, range_size));
            }
        }

        switch (response.code)
        {
            case 200: _stats->count_200++; break;
            case 206: _stats->count_206++; break;
            case 304: _stats->count_304++; break;
            default: break;
        }

        _stats->count++;
        _responses.insert(std::make_pair(ticket, std::move(response)));
        _finished.insert(ticket);
        return true;
    }

    void cancel_request(HttpTicket ticket) override
    {
        _responses.erase(ticket);
        _finished.erase(ticket);
    }

    void work() override {}

    std::optional<ResponseMetadata> dequeue_finished_request() override
    {
        if (!_finished.empty())
        {
            HttpTicket ticket = *_finished.begin();
            _finished.erase(ticket);

            auto it = _responses.find(ticket);

            ResponseMetadata metadata;
            metadata.code = it->second.code;
            metadata.content_type = std::string(it->second.headers.get_header("Content-Type"));
            metadata.headers.swap(it->second.headers);
            metadata.data_size = it->second.data.size();
            metadata.status = (metadata.code >= 200 && metadata.code < 400)
                ? HttpRequestStatus::Loaded
                : HttpRequestStatus::Error;
            metadata.ticket = ticket;

            return metadata;
        }
        else
        {
            return std::nullopt;
        }
    }

    bool copy_data(HttpTicket ticket, std::span<std::byte> dst) override
    {
        auto it = _responses.find(ticket);
        if (it != _responses.end() && dst.size_bytes() == it->second.data.size()
            && !it->second.data_dst.has_value())
        {
            memcpy(dst.data(), it->second.data.data(), it->second.data.size());
            it->second.data_dst = {dst};
            return true;
        }
        else
        {
            return false;
        }
    }

    bool is_data_copied(HttpTicket ticket) override
    {
        auto it = _responses.find(ticket);
        if (it != _responses.end())
        {
            return it->second.data_dst.has_value();
        }
        else
        {
            return false;
        }
    }

    void free_data(HttpTicket ticket) override { _responses.erase(ticket); }
};

class HttpTestClock : public IHttpClock
{
    // Jan 1st 2023, 20:00:00 GMT
    int64_t _time = 1672603200;

public:
    HttpTime now() const { return HttpTime(_time); }

    void advance_time(int64_t offset)
    {
        assert(offset > 0);
        _time += offset;
    }
};

HttpTicket generate_http_ticket()
{
    static uint64_t id = 1;
    return {id++};
}

Response get_response_from(
    IHttpLoader& loader,
    std::string_view url,
    std::initializer_list<std::pair<std::string_view, std::string_view>> in_headers = {},
    uint64_t range_start = 0,
    uint64_t range_size = 0)
{
    HttpTicket ticket = generate_http_ticket();

    HttpHeaders headers;
    for (const auto& h : in_headers)
    {
        headers.set_header(h.first, h.second);
    }

    EXPECT_TRUE(loader.start_request(ticket, url, range_start, range_size, headers));
    while (true)
    {
        loader.work();
        auto http_response_opt = loader.dequeue_finished_request();
        if (http_response_opt.has_value())
        {
            auto http_response = http_response_opt.value();
            EXPECT_EQ(http_response.ticket, ticket);
            Response response;
            response.code = http_response.code;
            response.headers.swap(http_response.headers);

            response.data.resize(http_response.data_size);
            loader.copy_data(ticket, {(std::byte*)response.data.data(), response.data.size()});
            assert(loader.is_data_copied(ticket));

            loader.free_data(ticket);
            return response;
        }
    }
}

TEST(DummyServer, NotFound)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader(new DummyServer(&clock, &stats));

    Response r = get_response_from(*loader, "https://horizon.tests/thisdoesntexist");
    EXPECT_EQ(r.code, 404);
}

TEST(DummyServer, DummyNakedSimple)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader(new DummyServer(&clock, &stats));

    Response r = get_response_from(*loader, "https://horizon.tests/hello");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Hello, world!");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
}

TEST(DummyServer, DummyServerClock)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader(new DummyServer(&clock, &stats));

    Response r = get_response_from(*loader, "https://horizon.tests/hello");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Hello, world!");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");

    clock.advance_time(90);
    r = get_response_from(*loader, "https://horizon.tests/hello");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Hello, world!");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:30 GMT");

    clock.advance_time(91);
    r = get_response_from(*loader, "https://horizon.tests/hello");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Hello, world!");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:03:01 GMT");

    EXPECT_EQ(stats.count_200, 3);
}

TEST(HttpCache, DummyCachedSimple)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r = get_response_from(*loader, "https://horizon.tests/hello");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Hello, world!");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
}

TEST(HttpCache, UncachedCounter)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r = get_response_from(*loader, "https://horizon.tests/counter");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/counter");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "30");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:30 GMT");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count, 2);

    clock.advance_time(45);
    r = get_response_from(*loader, "https://horizon.tests/counter");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "75");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:15 GMT");
    EXPECT_EQ(stats.count_200, 3);
    EXPECT_EQ(stats.count, 3);

    clock.advance_time(15);
    r = get_response_from(*loader, "https://horizon.tests/counter");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "90");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:30 GMT");
    EXPECT_EQ(stats.count_200, 4);
    EXPECT_EQ(stats.count, 4);

    clock.advance_time(5);
    r = get_response_from(*loader, "https://horizon.tests/counter");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "95");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:35 GMT");
    EXPECT_EQ(stats.count_200, 5);
    EXPECT_EQ(stats.count, 5);

    clock.advance_time(40);
    r = get_response_from(*loader, "https://horizon.tests/counter");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "135");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:02:15 GMT");
    EXPECT_EQ(stats.count_200, 6);
    EXPECT_EQ(stats.count, 6);
}

TEST(HttpCache, CachedCounterMaxAge)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r = get_response_from(*loader, "https://horizon.tests/counter_max_age");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/counter_max_age");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(r.headers.get_header("age"), "30");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(15);
    r = get_response_from(*loader, "https://horizon.tests/counter_max_age");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(r.headers.get_header("age"), "45");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/counter_max_age");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "75");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:15 GMT");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count, 2);

    clock.advance_time(15);
    r = get_response_from(*loader, "https://horizon.tests/counter_max_age");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "75");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:15 GMT");
    EXPECT_EQ(r.headers.get_header("age"), "15");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count, 2);

    clock.advance_time(60);
    r = get_response_from(*loader, "https://horizon.tests/counter_max_age");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "150");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:02:30 GMT");
    EXPECT_EQ(stats.count_200, 3);
    EXPECT_EQ(stats.count, 3);
}

TEST(HttpCache, CachedCounterExpires)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r = get_response_from(*loader, "https://horizon.tests/counter_expires");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/counter_expires");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(r.headers.get_header("age"), "30");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(15);
    r = get_response_from(*loader, "https://horizon.tests/counter_expires");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(r.headers.get_header("age"), "45");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/counter_expires");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "75");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:15 GMT");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count, 2);

    clock.advance_time(15);
    r = get_response_from(*loader, "https://horizon.tests/counter_expires");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "75");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:15 GMT");
    EXPECT_EQ(r.headers.get_header("age"), "15");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count, 2);

    clock.advance_time(60);
    r = get_response_from(*loader, "https://horizon.tests/counter_expires");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "150");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:02:30 GMT");
    EXPECT_EQ(stats.count_200, 3);
    EXPECT_EQ(stats.count, 3);
}

TEST(HttpCache, CachedCounterNoStore)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r = get_response_from(*loader, "https://horizon.tests/counter_no_store");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/counter_no_store");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "30");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:30 GMT");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count, 2);

    clock.advance_time(15);
    r = get_response_from(*loader, "https://horizon.tests/counter_no_store");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "45");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:45 GMT");
    EXPECT_EQ(stats.count_200, 3);
    EXPECT_EQ(stats.count, 3);
}

TEST(HttpCache, CachedCounterNoCache)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r = get_response_from(*loader, "https://horizon.tests/counter_no_cache");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/counter_no_cache");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "30");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:30 GMT");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count, 2);

    clock.advance_time(15);
    r = get_response_from(*loader, "https://horizon.tests/counter_no_cache");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "45");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:45 GMT");
    EXPECT_EQ(stats.count_200, 3);
    EXPECT_EQ(stats.count, 3);
}

TEST(HttpCache, CachedCounterVary)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r =
        get_response_from(*loader, "https://horizon.tests/counter_vary", {{"user-name", "Alice"}});
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Hello Alice, your counter is 0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/counter_vary", {{"user-name", "Alice"}});
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Hello Alice, your counter is 0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(r.headers.get_header("Age"), "30");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    r = get_response_from(*loader, "https://horizon.tests/counter_vary", {{"user-name", "Bob"}});
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Hello Bob, your counter is 30");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:30 GMT");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count, 2);

    clock.advance_time(45);
    r = get_response_from(*loader, "https://horizon.tests/counter_vary", {{"user-name", "Alice"}});
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Hello Alice, your counter is 75");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:15 GMT");
    EXPECT_EQ(stats.count_200, 3);
    EXPECT_EQ(stats.count, 3);

    r = get_response_from(*loader, "https://horizon.tests/counter_vary", {{"user-name", "Bob"}});
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Hello Bob, your counter is 30");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:30 GMT");
    EXPECT_EQ(r.headers.get_header("Age"), "45");
    EXPECT_EQ(stats.count_200, 3);
    EXPECT_EQ(stats.count, 3);

    r = get_response_from(
        *loader, "https://horizon.tests/counter_vary",
        {{"user-name", "Bob"}, {"Cheese", "Camembert"}});
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Hello Bob, your counter is 75");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:15 GMT");
    EXPECT_EQ(stats.count_200, 4);
    EXPECT_EQ(stats.count, 4);
}

TEST(HttpCache, UncachedHeaders)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r = get_response_from(*loader, "https://horizon.tests/uncached_headers");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.headers.get_header("Upgrade"), "Yes-Please");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/uncached_headers");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.headers.get_header("Upgrade"), "");
    EXPECT_EQ(r.headers.get_header("Age"), "30");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(90);
    r = get_response_from(*loader, "https://horizon.tests/uncached_headers");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.headers.get_header("Upgrade"), "Yes-Please");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count, 2);
}

// For now we don't handle range requests correctly in the cache.
// Fragment should be combined, and previous requests should be able to respond
// to newer requests when entirely contained in them.
// @Todo(http_cache) Range requests
TEST(HttpCache, CachedRange)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r = get_response_from(*loader, "https://horizon.tests/cached_range", {}, 5, 10);
    EXPECT_EQ(r.code, 206);
    EXPECT_EQ(r.data, "56789 - 0 ");
    EXPECT_EQ(stats.count, 1);
    EXPECT_EQ(stats.count_206, 1);

    clock.advance_time(45);
    r = get_response_from(*loader, "https://horizon.tests/cached_range", {}, 5, 10);
    EXPECT_EQ(r.code, 206);
    EXPECT_EQ(r.data, "56789 - 0 ");
    EXPECT_EQ(stats.count, 1);
    EXPECT_EQ(stats.count_206, 1);

    r = get_response_from(*loader, "https://horizon.tests/cached_range", {}, 10, 5);
    EXPECT_EQ(r.code, 206);
    EXPECT_EQ(r.data, " - 45");
    EXPECT_EQ(stats.count, 2);
    EXPECT_EQ(stats.count_206, 2);

    r = get_response_from(*loader, "https://horizon.tests/cached_range", {}, 5, 10);
    EXPECT_EQ(r.code, 206);
    EXPECT_EQ(r.data, "56789 - 0 ");
    EXPECT_EQ(stats.count, 2);
    EXPECT_EQ(stats.count_206, 2);

    r = get_response_from(*loader, "https://horizon.tests/cached_range", {}, 10, 5);
    EXPECT_EQ(r.code, 206);
    EXPECT_EQ(r.data, " - 45");
    EXPECT_EQ(stats.count, 2);
    EXPECT_EQ(stats.count_206, 2);

    clock.advance_time(45);
    r = get_response_from(*loader, "https://horizon.tests/cached_range", {}, 5, 10);
    EXPECT_EQ(r.code, 206);
    EXPECT_EQ(r.data, "56789 - 90");
    EXPECT_EQ(stats.count, 3);
    EXPECT_EQ(stats.count_206, 3);

    r = get_response_from(*loader, "https://horizon.tests/cached_range", {}, 10, 5);
    EXPECT_EQ(r.code, 206);
    EXPECT_EQ(r.data, " - 45");
    EXPECT_EQ(stats.count, 3);
    EXPECT_EQ(stats.count_206, 3);
}

TEST(HttpCache, StaticEtag)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r = get_response_from(*loader, "https://horizon.tests/static_etag");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "This is static");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 0);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(45);
    r = get_response_from(*loader, "https://horizon.tests/static_etag");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "This is static");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(r.headers.get_header("age"), "45");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 0);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(45);
    r = get_response_from(*loader, "https://horizon.tests/static_etag");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "This is static");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(r.headers.get_header("age"), "90");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 1);
    EXPECT_EQ(stats.count, 2);

    clock.advance_time(45);
    r = get_response_from(*loader, "https://horizon.tests/static_etag");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "This is static");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(r.headers.get_header("age"), "135");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 2);
    EXPECT_EQ(stats.count, 3);

    clock.advance_time(45);
    r = get_response_from(*loader, "https://horizon.tests/static_etag");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "This is static");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(r.headers.get_header("age"), "180");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 3);
    EXPECT_EQ(stats.count, 4);
}

TEST(HttpCache, StaticEtagWithChangingDate)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r = get_response_from(*loader, "https://horizon.tests/static_etag_changing_date");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "This is static");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 0);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(45);
    r = get_response_from(*loader, "https://horizon.tests/static_etag_changing_date");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "This is static");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(r.headers.get_header("age"), "45");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 0);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(45);
    r = get_response_from(*loader, "https://horizon.tests/static_etag_changing_date");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "This is static");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:30 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 1);
    EXPECT_EQ(stats.count, 2);

    clock.advance_time(45);
    r = get_response_from(*loader, "https://horizon.tests/static_etag_changing_date");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "This is static");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:30 GMT");
    EXPECT_EQ(r.headers.get_header("age"), "45");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 1);
    EXPECT_EQ(stats.count, 2);

    clock.advance_time(45);
    r = get_response_from(*loader, "https://horizon.tests/static_etag_changing_date");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "This is static");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:03:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 2);
    EXPECT_EQ(stats.count, 3);
}

TEST(HttpCache, DynamicEtag)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r = get_response_from(*loader, "https://horizon.tests/dynamic_etag");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Version 0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 0);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/dynamic_etag");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Version 0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 1);
    EXPECT_EQ(stats.count, 2);

    r = get_response_from(*loader, "https://horizon.tests/dynamic_etag");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Version 0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 2);
    EXPECT_EQ(stats.count, 3);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/dynamic_etag");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Version 1");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count_304, 2);
    EXPECT_EQ(stats.count, 4);

    r = get_response_from(*loader, "https://horizon.tests/dynamic_etag");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Version 1");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count_304, 3);
    EXPECT_EQ(stats.count, 5);

    clock.advance_time(60);
    r = get_response_from(*loader, "https://horizon.tests/dynamic_etag");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Version 2");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(stats.count_200, 3);
    EXPECT_EQ(stats.count_304, 3);
    EXPECT_EQ(stats.count, 6);

    clock.advance_time(120);
    r = get_response_from(*loader, "https://horizon.tests/dynamic_etag");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Version 4");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(stats.count_200, 4);
    EXPECT_EQ(stats.count_304, 3);
    EXPECT_EQ(stats.count, 7);
}

TEST(HttpCache, LastModified)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r = get_response_from(*loader, "https://horizon.tests/last_modified");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Version 0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 0);
    EXPECT_EQ(stats.count, 1);

    r = get_response_from(*loader, "https://horizon.tests/last_modified");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Version 0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 1);
    EXPECT_EQ(stats.count, 2);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/last_modified");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Version 0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count_304, 2);
    EXPECT_EQ(stats.count, 3);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/last_modified");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Version 1");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:00 GMT");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count_304, 2);
    EXPECT_EQ(stats.count, 4);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/last_modified");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "Version 1");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:01:00 GMT");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count_304, 3);
    EXPECT_EQ(stats.count, 5);
}

TEST(HttpCache, ClearCache)
{
    RequestsStats stats = {};
    HttpTestClock clock;
    std::unique_ptr<IHttpLoader> loader = create_http_cache_loader(
        &clock, std::make_unique<DummyServer>(&clock, &stats), DefaultCacheSize);

    Response r = get_response_from(*loader, "https://horizon.tests/counter_max_age");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    clock.advance_time(30);
    r = get_response_from(*loader, "https://horizon.tests/counter_max_age");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "0");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:00 GMT");
    EXPECT_EQ(stats.count_200, 1);
    EXPECT_EQ(stats.count, 1);

    loader->clear_cache();

    r = get_response_from(*loader, "https://horizon.tests/counter_max_age");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.data, "30");
    EXPECT_EQ(r.headers.get_header("content-type"), "text/plain");
    EXPECT_EQ(r.headers.get_header("date"), "Sun, 01 Jan 2023 20:00:30 GMT");
    EXPECT_EQ(stats.count_200, 2);
    EXPECT_EQ(stats.count, 2);
}
