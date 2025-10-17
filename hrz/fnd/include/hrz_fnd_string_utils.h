#pragma once

#include <fmt/core.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hrz::str
{

// Strings are considered to be UTF-8.
// The `_s` variants don't allocate memory, so are more lighweight, but the
// caller is in charge of handling the lifetime of the strings correctly.

/**
 * Removes the last characters that are equal to c.
 * This only works if c is an ASCII character.
 * Example: rtrim("aaabbbaaa", 'a') => "aaabbb"
 * THe variants that have no character parameter trim all whitespaces (C's isspace).
 */
std::string_view rtrim_s(std::string_view str, char c);
std::string rtrim(std::string_view str, char c);
std::string_view rtrim_s(std::string_view str);
std::string rtrim(std::string_view str);

/**
 * Removes the first characters that are equal to c.
 * This only works if c is an ASCII character.
 * Example: ltrim("aaabbbaaa", 'a') => "bbbaaa"
 * THe variants that have no character parameter trim all whitespaces (C's isspace).
 */
std::string_view ltrim_s(std::string_view str, char c);
std::string ltrim(std::string_view str, char c);
std::string_view ltrim_s(std::string_view str);
std::string ltrim(std::string_view str);

/**
 * Does both ltrim and rtrim. See their documentation for more info.
 */
std::string_view trim_s(std::string_view str, char c);
std::string trim(std::string_view str, char c);
std::string_view trim_s(std::string_view str);
std::string trim(std::string_view str);

/**
 * Returns the index of the last character c in str.
 * This only works if c is an ASCII character.
 * -1 means the character was not found.
 * Example: rfind("abab", 'a') => 2
 */
int rfind(std::string_view str, char c);

/**
 * Returns the index of the first character c in str.
 * This only works if c is an ASCII character.
 * -1 means the character was not found.
 * Example: rfind("abab", 'a') => 0
 */
int find(std::string_view str, char c);

/**
 * Split the input string at the first given character.
 * Return the part before and after the character, not including it.
 * If the character is not found, return the full input string first, and an
 * empty string second.
 */
std::pair<std::string_view, std::string_view> split(std::string_view str, char c);

/**
 * Returns whether `str` starts with `start` or not.
 */
bool starts_with(std::string_view str, std::string_view start);

/**
 * Returns whether `str` starts with `start` or not, case insensitive.
 * Only for ASCII strings, and without locale handling.
 */
bool istarts_with(std::string_view str, std::string_view start);

/**
 * Returns whether `str` ends with `end` or not.
 */
bool ends_with(std::string_view str, std::string_view end);

/**
 * Returns whether `str` ends with `end` or not, case insensitive.
 * Only for ASCII strings, and without locale handling.
 */
bool iends_with(std::string_view str, std::string_view end);

/**
 * Returns whether `a` and `b` are the same, disregarding case differences.
 * Only for ASCII strings, and without locale handling.
 */
bool iequals(std::string_view a, std::string_view b);

/**
 * Defines variants of base64 decoding.
 */
enum class Base64DecodingVariant
{
    Normal,      /// '+', '/', '='
    UrlFilename, /// '-', '_', '='
    Both,        /// In this mode we join both decoding tables. '+' == '-', '/' == '_'.
};

/**
 * Defines variants of base64 encoding.
 */
enum class Base64EncodingVariant
{
    Normal,      /// '+', '/', '='
    UrlFilename, /// '-', '_', '='
};

/**
 * Returns the size of the decoded base64 data represented by `str`.
 * This only looks at the size and end of the string, no actual
 * validity check is performed.
 */
size_t decode_base64_size_hint(
    std::string_view str,
    Base64DecodingVariant variant = Base64DecodingVariant::Normal);

/**
 * Decodes a base64 string `str` and writes its content to `data`.
 * `data` must point to a space large enough to accomodate the data.
 * See `decode_base64_size_hint`.
 * Returns how many bytes were written to `data`.
 * Note that even if it was unsuccessful, some data may have been
 * written to `data`.
 * If the number of bytes decoded is not equal to what `decode_base64_size_hint`
 * returned, it means the data was corrupted somehow.
 */
size_t decode_base64_s(
    std::string_view str,
    std::span<std::byte> data,
    Base64DecodingVariant variant = Base64DecodingVariant::Normal);

/**
 * Convenience function that handles allocation.
 * See previous declaration for details.
 * The content is appended to `data`.
 */
size_t decode_base64(
    std::string_view str,
    std::vector<std::byte>* data,
    Base64DecodingVariant variant = Base64DecodingVariant::Normal);

/**
 * Encodes a block of binary data into base64, and appends the result to `str`.
 * Returns the amount of characters written.
 */
size_t encode_base64(
    std::span<const std::byte> data,
    std::string* str,
    Base64EncodingVariant variant = Base64EncodingVariant::Normal);

/**
 * Transcodes a 32-bits Unicode code point (U+XXXXXXXX) into UTF-8 bytes. Returns the number of
 * bytes written in `buffer` (between 0 and 4).
 * `buffer` must be large enough to contain the result.
 */
size_t encode_code_point_to_utf8(uint32_t code_point, std::span<char> buffer);

/**
 * Remove fmt named arguments from fmt_string that are not in valid_args.
 * This is important because fmt will crash if it finds a named argument that is not defined.
 * Warning: This will not work for strings with escaped arguments, i.e. containing "{{" or "}}".
 */
std::string sanitize_named_fmt_arguments(
    std::string fmt_string,
    std::span<const std::string_view> valid_args);

/**
 * Parses a string into the given types. Uses the C locale. Return nullopt if the parsing fails.
 */
std::optional<int64_t> parse_int64(std::string_view str);
std::optional<uint64_t> parse_uint64(std::string_view str);
std::optional<double> parse_double(std::string_view str);

} // namespace hrz::str
