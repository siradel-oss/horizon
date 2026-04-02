#pragma once

#include <string>
#include <string_view>

namespace hrz
{

// This class reproduces part of the functionality in
// https://github.com/CesiumGS/cesium/blob/451426bb5ea13797453e652c611024d5e547352f/Source/Core/Resource.js
//
// It is used to access layer data whose formats are defined by the Cesium team
// the same way as Cesium does.
class BaseUrl
{
public:
    BaseUrl() :
        base_url(""),
        base_query(""),
        derivation_base_url(""),
        default_base_query_parameter_inclusion(false)
    {
    }

    BaseUrl(std::string_view base_url, bool default_base_query_parameter_inclusion);

    void add_slash();

    std::string base() const;

    BaseUrl derive_base(std::string_view url) const;

    std::string derive(std::string_view url, bool include_base_query_parameters) const;
    std::string derive(std::string_view url) const;

private:
    std::string base_url;
    std::string base_query;
    std::string derivation_base_url;

    bool default_base_query_parameter_inclusion;
};

} // namespace hrz
