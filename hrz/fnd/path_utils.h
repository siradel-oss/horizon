#pragma once

#include <string>
#include <string_view>

namespace hrz::path
{

// All paths must use forward slashes.
// Consecutive slashes are considered as a single one.
// The _s variants don't allocate memory, so are more lighweight, but the
// caller is in charge of handling the lifetime of the strings correctly.

/**
 * Removes the last component of a path.
 * Example: /my/path/file.txt => /my/path
 */
std::string_view parent_s(std::string_view path);
std::string parent(std::string_view path);

/**
 * Returns the last file or directory name.
 * First trims slashes on the right.
 * Exmaple: /my/path/file.txt => file.txt
 */
std::string_view basename_s(std::string_view path);
std::string basename(std::string_view path);

/**
 * Joins different parts of a path with the appropriate separators.
 * Each part is first trimmed of its separators expect for the
 * first one which is only trimmed right.
 * Example:
 * {"/hello/", "/world", "bye/"} => "/hello/world/bye"
 */
std::string join(std::initializer_list<std::string_view> parts);

} // namespace hrz::path
