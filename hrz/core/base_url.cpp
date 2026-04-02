#include "hrz/core/base_url.h"

#include "hrz/fnd/url_utils.h"

namespace hrz
{

BaseUrl::BaseUrl(std::string_view base, bool default_base_query_parameter_inclusion)
{
    base_url = url::without_query(base);
    base_query = url::query(base);

    if (!base_url.empty() && base_url.back() != '/')
    {
        derivation_base_url = url::parent_directory(base_url);
    }
    else
    {
        derivation_base_url = base_url;
    }

    this->default_base_query_parameter_inclusion = default_base_query_parameter_inclusion;
}

void BaseUrl::add_slash()
{
    derivation_base_url = url::add_slash(base_url);
}

std::string BaseUrl::base() const
{
    std::string url(base_url);

    if (!base_query.empty())
    {
        url += "?" + base_query;
    }

    return url;
}

BaseUrl BaseUrl::derive_base(std::string_view url) const
{
    return {derive(url, true), default_base_query_parameter_inclusion};
}

std::string BaseUrl::derive(std::string_view url, bool include_base_query_parameters) const
{
    auto derived_url = url::join({derivation_base_url, url::without_query_s(url)});

    bool first_query = true;

    if (include_base_query_parameters && !base_query.empty())
    {
        derived_url += '?';
        derived_url += base_query;
        first_query = false;
    }

    auto query = url::query_s(url);
    if (!query.empty())
    {
        derived_url += first_query ? '?' : '&';
        derived_url += query;
    }

    return derived_url;
}

std::string BaseUrl::derive(std::string_view url) const
{
    return derive(url, default_base_query_parameter_inclusion);
}

} // namespace hrz
