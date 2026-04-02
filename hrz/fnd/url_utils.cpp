#include "hrz/fnd/url_utils.h"

#include "hrz/fnd/char_utils.h"
#include "hrz/fnd/string_utils.h"

#include <cassert>
#include <sstream>

namespace
{

inline bool is_domain_char(const char c)
{
    return hrz::is_ascii_alpha_numeric(c) || c == '.' || c == '-' || c == '_';
}

inline bool is_unreserved_char(const char c)
{
    return is_domain_char(c) || c == '~';
}

} // namespace

namespace hrz::url
{

bool is_absolute(std::string_view url)
{
    if (url.empty()) return false;
    if (is_protocol_relative(url)) return true;

    // protocol ':'
    // See https://www.rfc-editor.org/rfc/rfc3986#section-3
    for (unsigned int i = 0; i < url.size(); ++i)
    {
        auto c = url.data()[i];

        if (c == ':') return true;
        if (!hrz::is_ascii_lowercase_letter(c)) return false;
    }

    return false;
}

bool is_relative(std::string_view url)
{
    return !is_absolute(url);
}

bool is_protocol_relative(std::string_view url)
{
    return url.size() >= 2 && url.data()[0] == '/' && url.data()[1] == '/';
}

std::string_view protocol_s(std::string_view url)
{
    if (url.empty()) return {};

    auto first_char = url.data()[0];

    if (first_char == '/')
    {
        // Protocol-relative URL.
        return {};
    }

    if (!hrz::is_ascii_lowercase_letter(first_char))
    {
        // Malformed URL.
        return {};
    }

    // protocol ':'
    for (unsigned int i = 1; i < url.size(); ++i)
    {
        auto c = url.data()[i];

        if (c == ':')
            return {url.data(), i};
        else if (!hrz::is_ascii_lowercase_letter(c))
        {
            return {};
        }
    }

    return {};
}

std::string protocol(std::string_view url)
{
    return std::string(protocol_s(url));
}

std::string_view domain_s(std::string_view url)
{
    if (url.empty()) return {};

    auto first_char = url.data()[0];

    size_t domain_start = 0;

    if (first_char == '/')
    {
        // Protocol-relative URL.
        if (url.size() >= 2 && url.data()[1] == '/')
        {
            domain_start = 2;
        }
        else
        {
            return {};
        }
    }
    else
    {
        if (!hrz::is_ascii_lowercase_letter(first_char))
        {
            // Malformed URL.
            return {};
        }

        // 0: protocol
        // 1: ':'
        // 2: first '/'
        unsigned int step = 0;
        for (unsigned int i = 1; i < url.size() && domain_start == 0; ++i)
        {
            auto c = url.data()[i];

            switch (step)
            {
                case 0:
                    if (c == ':')
                    {
                        step = 1;
                    }
                    else if (!hrz::is_ascii_lowercase_letter(c))
                    {
                        return {};
                    }
                    break;
                case 1:
                    if (c == '/')
                    {
                        step = 2;
                    }
                    else
                    {
                        return {};
                    }
                    break;
                case 2:
                    if (c == '/')
                    {
                        domain_start = i + 1;
                    }
                    else
                    {
                        return {};
                    }
                    break;
                default: assert(false && "Invalid state"); break;
            }
        }
    }

    if (domain_start == 0)
    {
        // A URL cannot start with its domain name (unless it is preceded by "//").
        return {};
    }

    for (unsigned int i = domain_start; i < url.size(); ++i)
    {
        auto c = url.data()[i];

        if (!is_domain_char(c))
        {
            return {url.data() + domain_start, i - domain_start};
        }
    }

    return {url.data() + domain_start, url.size() - domain_start};
}

std::string domain(std::string_view url)
{
    return std::string(domain_s(url));
}

std::string_view authority_s(std::string_view url)
{
    auto domain = domain_s(url);

    if (domain.empty()) return domain;

    size_t domain_start = domain.data() - url.data();
    size_t domain_end = domain_start + domain.size();

    bool in_port = false;
    for (unsigned int i = domain_end; i < url.size(); ++i)
    {
        auto c = url.data()[i];

        if (in_port)
        {
            if (!hrz::is_ascii_digit(c))
            {
                if (c == '/')
                {
                    return {domain.data(), i - domain_start};
                }
                else
                {
                    return {};
                }
            }
        }
        else if (i == domain_end && c == ':')
        {
            in_port = true;
        }
        else
        {
            return domain;
        }
    }

    return {domain.data(), url.size() - domain_start};
}

std::string authority(std::string_view url)
{
    return std::string(authority_s(url));
}

std::string_view path_s(std::string_view url)
{
    auto authority = authority_s(url);

    size_t path_begin = 0;
    if (authority.empty())
    {
        auto protocol = protocol_s(url);
        if (!protocol.empty())
        {
            if (protocol.size() + 2 < url.size() && url[protocol.size() + 1] == '/'
                && url[protocol.size() + 2] == '/')
            {
                // protocol '://' path
                path_begin = protocol.size() + 3;
            }
            else
            {
                // protocol ':' path
                path_begin = protocol.size() + 1;
            }
        }
    }
    else
    {
        path_begin = authority.data() - url.data() + authority.size();
    }

    if (path_begin >= url.size()) return {};

    for (unsigned int i = path_begin; i < url.size(); ++i)
    {
        auto c = url.data()[i];

        if (c == '?' || c == '#')
        {
            return {url.data() + path_begin, i - path_begin};
        }
    }

    return {url.data() + path_begin, url.size() - path_begin};
}

std::string path(std::string_view url)
{
    return std::string(path_s(url));
}

std::string_view query_s(std::string_view url)
{
    bool in_query = false;
    size_t query_start = 0;

    for (unsigned int i = 0; i < url.size(); ++i)
    {
        auto c = url.data()[i];

        if (c == '?')
        {
            in_query = true;
            query_start = i + 1;
        }
        else if (c == '#' && in_query)
        {
            return {url.data() + query_start, i - query_start};
        }
    }

    if (in_query)
    {
        return {url.data() + query_start, url.size() - query_start};
    }

    return {};
}

std::string query(std::string_view url)
{
    return std::string(query_s(url));
}

std::string_view anchor_s(std::string_view url)
{
    for (unsigned int i = url.size(); i > 0; --i)
    {
        auto c = url.data()[i - 1];

        if (c == '#')
        {
            return {url.data() + i, url.size() - i};
        }
    }

    return {};
}

std::string anchor(std::string_view url)
{
    return std::string(anchor_s(url));
}

std::string_view parent_directory_s(std::string_view url)
{
    for (unsigned int i = url.size(); i > 0; --i)
    {
        auto c = url.data()[i - 1];

        if (c == '/')
        {
            if (i == 2 && url.data()[0] == '/')
            {
                // Beginning of a protocol-relative URL.
                return url;
            }

            auto protocol = protocol_s(url);
            if (protocol.data() != nullptr && protocol.size() + 3 == i)
            {
                // Beginning of an absolute URL.
                return url;
            }

            return {url.data(), i};
        }
        else if (c == ':')
        {
            // Hostless URL.
            return {url.data(), i};
        }
    }

    return {};
}

std::string parent_directory(std::string_view url)
{
    return std::string(parent_directory_s(url));
}

std::string_view without_query_s(std::string_view url)
{
    for (unsigned int i = 0; i < url.size(); ++i)
    {
        auto c = url.data()[i];

        if (c == '?' || c == '#')
        {
            return {url.data(), i};
        }
    }

    return url;
}

std::string without_query(std::string_view url)
{
    return std::string(without_query_s(url));
}

std::string_view query_parameter_value_s(std::string_view url, std::string_view parameter_key)
{
    return parameter_value_from_query_s(query_s(url), parameter_key);
}

std::string query_parameter_value(std::string_view url, std::string_view parameter_key)
{
    return std::string(query_parameter_value_s(url, parameter_key));
}

std::string_view parameter_value_from_query_s(
    std::string_view query,
    std::string_view parameter_key)
{
    if (parameter_key.empty()) return {};

    // 0: not in a parameter,
    // 1: in a candidate parameter key,
    // 2: in the desired value,
    // 3: in a rejected parameter.
    unsigned int state = 0;
    size_t key_i = 0;

    size_t value_start = 0;

    for (size_t i = 0; i < query.size(); ++i)
    {
        auto c = query.data()[i];

        switch (state)
        {
            case 0:
            {
                if (c == parameter_key.data()[0])
                {
                    state = 1;
                    key_i = 1;
                }
                else if (c != '&')
                {
                    state = 3;
                }
                break;
            }
            case 1:
            {
                if (c == '=')
                {
                    if (key_i == parameter_key.size())
                    {
                        value_start = i + 1;
                        state = 2;
                    }
                    else
                    {
                        state = 3;
                    }
                }
                else if (key_i >= parameter_key.size())
                {
                    state = 3;
                }
                else if (c != parameter_key.data()[key_i])
                {
                    state = 3;
                }
                else
                {
                    key_i += 1;
                }
                break;
            }
            case 2:
            {
                if (c == '&' || c == ';')
                {
                    return {query.data() + value_start, i - value_start};
                }
                break;
            }
            case 3:
            {
                if (c == '&' || c == ';')
                {
                    state = 0;
                }
                break;
            }
            default: assert(false && "Unhandled case");
        }
    }

    if (state == 2)
    {
        return {query.data() + value_start, query.size() - value_start};
    }

    return {};
}

std::string parameter_value_from_query(std::string_view query, std::string_view parameter_key)
{
    return std::string(parameter_value_from_query_s(query, parameter_key));
}

std::string add_slash(std::string_view url)
{
    auto base = without_query(url);

    if (!base.empty() && base.back() != '/')
    {
        base += '/';
    }

    auto query = url::query(url);

    if (query.empty())
    {
        return base;
    }
    else
    {
        return base + "?" + query;
    }
}

std::string join(std::span<const std::string_view> parts)
{
    if (parts.empty()) return {};

    bool has_protocol = false;
    size_t last_protocol_part = 0;

    bool has_domain = false;
    size_t last_domain_part = 0;

    bool has_absolute_path = false;
    size_t last_absolute_path_part = 0;

    size_t last_non_pure_query_part = 0;

    for (unsigned int i = 0; i < parts.size(); ++i)
    {
        auto& part = parts.begin()[i];

        if (is_absolute(part))
        {
            auto protocol = protocol_s(part);
            if (protocol.data() != nullptr)
            {
                has_protocol = true;
                last_protocol_part = i;
            }

            auto domain = domain_s(part);
            if (domain.data() != nullptr)
            {
                has_domain = true;
                last_domain_part = i;
            }

            has_absolute_path = true;
            last_absolute_path_part = i;

            last_non_pure_query_part = i;
        }
        else
        {
            if (part.size() > 0)
            {
                char first_char = part.data()[0];

                if (first_char == '/')
                {
                    has_absolute_path = true;
                    last_absolute_path_part = i;
                }

                if (first_char != '?')
                {
                    last_non_pure_query_part = i;
                }
            }
        }
    }

    std::ostringstream sstr;

    if (has_protocol && (has_domain && last_protocol_part != last_domain_part))
    {
        assert(last_domain_part >= last_protocol_part);

        // Last domain was protocol-relative, but a protocol was given earlier.
        sstr << protocol_s(parts.begin()[last_protocol_part]) << ':';
    }
    else if (
        has_protocol && !has_domain && has_absolute_path
        && last_protocol_part != last_absolute_path_part)
    {
        assert(last_absolute_path_part >= last_protocol_part);

        // A protocol without domain (ie. "file:") precedes an absolute path.
        sstr << protocol_s(parts.begin()[last_protocol_part]) << ':';
    }

    size_t next_part = 0;

    if (has_domain)
    {
        auto& part = parts.begin()[last_domain_part];

        if (has_absolute_path && last_domain_part != last_absolute_path_part)
        {
            assert(last_absolute_path_part >= last_domain_part);

            // The path part was restarted after the domain.
            sstr << protocol_s(part) << "://" << authority_s(part);

            if (last_absolute_path_part < last_non_pure_query_part)
            {
                sstr << parent_directory_s(parts.begin()[last_absolute_path_part]);
            }
            else
            {
                sstr << parts.begin()[last_absolute_path_part];
            }
        }
        else
        {
            assert(last_absolute_path_part == last_domain_part);

            if (last_domain_part < last_non_pure_query_part)
            {
                auto base = parent_directory_s(part);
                sstr << base;

                if (base.back() != '/')
                {
                    sstr << '/';
                }
            }
            else
            {
                sstr << part;

                if (path_s(part).empty())
                {
                    sstr << '/';
                }
            }
        }

        next_part = last_absolute_path_part + 1;
    }
    else if (has_absolute_path)
    {
        sstr << parts.begin()[last_absolute_path_part];
        next_part = last_absolute_path_part + 1;
    }

    for (unsigned int i = next_part; i < parts.size(); ++i)
    {
        auto& part = parts.begin()[i];

        if (i < last_non_pure_query_part)
        {
            sstr << parent_directory_s(part);
        }
        else
        {
            sstr << without_query_s(part);
        }
    }

    // Only the last part can contribute to query parameters.
    {
        const auto& last_part = parts[parts.size() - 1];
        auto query = query_s(last_part);

        if (!query.empty())
        {
            sstr << "?" << query;
        }
    }

    return sstr.str();
}

std::string join(std::initializer_list<std::string_view> parts)
{
    return join((std::span<const std::string_view>)parts);
}

std::string percent_encode(std::string_view str)
{
    auto to_hex_digit = [](uint8_t d) -> char
    {
        if (d <= 9) return '0' + d;
        if (d >= 0xa && d <= 0xf) return 'A' + (d - 0xa);
        assert(false);
        return '?';
    };

    std::string res = "";
    res.reserve(str.size());

    for (size_t i = 0; i < str.size(); ++i)
    {
        auto c = str.data()[i];

        if (is_unreserved_char(c))
        {
            res += c;
        }
        else
        {
            res += '%';
            res += to_hex_digit(((uint8_t)c & 0xf0) >> 4);
            res += to_hex_digit((uint8_t)c & 0x0f);
        }
    }

    return res;
}

bool parse_data_url_s(std::string_view url, EncodedData* result)
{
    if (!url.starts_with("data:")) return false;
    url = url.substr(5); // Skip data:

    const int payload_start = str::find(url, ',');
    if (payload_start == -1) return false;

    result->encoded_payload = url.substr(payload_start + 1);
    result->mime_type = parse_mime(url.substr(0, payload_start));

    return true;
}

std::string append_query_parameters(
    std::string_view url,
    std::initializer_list<std::pair<std::string_view, std::string_view>> query_parameters)
{
    if (query_parameters.size() == 0) return std::string(url);

    auto base_query = query_s(url);
    auto base_url = without_query_s(url);

    std::stringstream sstr;

    if (!base_query.empty())
    {
        sstr << "?" << base_query;
    }

    for (auto& param : query_parameters)
    {
        if (sstr.tellp() > 0)
        {
            sstr << "&";
        }
        else if (base_query.empty())
        {
            sstr << "?";
        }

        sstr << param.first << "=" << param.second;
    }

    return join({base_url, sstr.str()});
}

} // namespace hrz::url
