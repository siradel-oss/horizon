#include "hrz/fnd/path_utils.h"

#include "hrz/fnd/string_utils.h"

#include <sstream>

namespace hrz::path
{

std::string_view parent_s(std::string_view path)
{
    path = str::rtrim_s(path, '/');

    int index = str::rfind(path, '/');
    if (index >= 0)
    {
        return str::rtrim_s(path.substr(0, index), '/');
    }
    else
    {
        return {};
    }
}

std::string parent(std::string_view path)
{
    return std::string(parent_s(path));
}

std::string_view basename_s(std::string_view path)
{
    path = str::rtrim_s(path, '/');

    int index = str::rfind(path, '/');
    if (index >= 0)
    {
        return path.substr(index + 1);
    }
    else
    {
        return path;
    }
}

std::string basename(std::string_view path)
{
    return std::string(basename_s(path));
}

std::string join(std::initializer_list<std::string_view> parts)
{
    std::ostringstream sstr;
    bool first = true;

    for (std::string_view part : parts)
    {
        if (!first)
        {
            sstr << "/";
            part = str::ltrim_s(part, '/');
        }

        sstr << str::rtrim_s(part, '/');
        first = false;
    }

    return sstr.str();
}

} // namespace hrz::path
