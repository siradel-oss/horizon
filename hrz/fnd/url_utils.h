#pragma once

#include "hrz/fnd/mime.h"

#include <span>
#include <string>
#include <string_view>

// See https://developer.mozilla.org/en-US/docs/Learn/Common_questions/What_is_a_URL

namespace hrz::url
{

bool is_absolute(std::string_view url);
bool is_relative(std::string_view url);
bool is_protocol_relative(std::string_view url);

std::string_view protocol_s(std::string_view url);
std::string protocol(std::string_view url);

std::string_view domain_s(std::string_view url);
std::string domain(std::string_view url);

std::string_view authority_s(std::string_view url);
std::string authority(std::string_view url);

std::string_view path_s(std::string_view url);
std::string path(std::string_view url);

std::string_view query_s(std::string_view url);
std::string query(std::string_view url);

std::string_view anchor_s(std::string_view url);
std::string anchor(std::string_view url);

std::string_view parent_directory_s(std::string_view url);
std::string parent_directory(std::string_view url);

std::string_view without_query_s(std::string_view url);
std::string without_query(std::string_view url);

std::string_view query_parameter_value_s(std::string_view url, std::string_view parameter_key);
std::string query_parameter_value(std::string_view url, std::string_view parameter_key);

std::string_view parameter_value_from_query_s(
    std::string_view query,
    std::string_view parameter_key);
std::string parameter_value_from_query(std::string_view query, std::string_view parameter_key);

// Add a slash at the end of the (non-query part of the) URL,
// if not present.
// This turns the last part of the path into a directory.
std::string add_slash(std::string_view url);

std::string join(std::span<const std::string_view> parts);
std::string join(std::initializer_list<std::string_view> parts);

std::string append_query_parameters(
    std::string_view url,
    std::initializer_list<std::pair<std::string_view, std::string_view>> query_parameters);

std::string percent_encode(std::string_view str);

struct EncodedData
{
    std::string_view encoded_payload;
    ParsedMime mime_type;

    bool is_base64() const { return mime_type.has_parameter("base64"); }
};

bool parse_data_url_s(std::string_view url, EncodedData* result);

} // namespace hrz::url
