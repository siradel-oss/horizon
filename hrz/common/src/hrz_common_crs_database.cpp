#include "hrz_common_crs_database.h"

#include <hrz_common_crs_utils.h>
#include <hrz_fnd_class.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_string_utils.h>

#include <gsl/gsl-lite.hpp>

#include <optional>

namespace
{

const std::unique_ptr<pl_CrsDatabase, decltype(&pl_destroy_crs_database)> CRS_DB(
    pl_load_crs_database(),
    &pl_destroy_crs_database);

std::optional<unsigned int> get_epsg_code(std::string_view srid_string)
{
    auto srid = hrz::crs::parse_srid(srid_string);
    if (srid.has_value())
    {
        if (srid->authority == "EPSG") return {srid->code};

        if (srid->authority == "OGC" || srid->authority == "CRS")
        {
            // Technically CRS84 is lon-lat, and EPSG:4326 is lat-lon.
            // However many pieces of data and software (including Horizon)
            // use the lon-lat order for EPSG:4326, making the two coordi-
            // nate systems equivalent.
            if (srid->code == 84) return {4326};
        }

        if (srid->authority == "OSGEO")
        {
            if (srid->code == 41001) return {3857};
        }
    }

    return std::nullopt;
}

bool init_crs_from_proj4_string(pl_Crs* crs, std::string_view proj4_string)
{
    std::memset(crs, 0, sizeof(pl_Crs));
    auto res = pl_crs_from_proj_str(proj4_string.data(), proj4_string.size(), crs);
    if (res == pl_Result_Ok)
    {
        return true;
    }

    HRZ_LOG_ERROR(
        "Could not create projection from string \"{}\": {} ({})", proj4_string,
        pl_result_string(res), fmt::underlying(res));
    return false;
}

bool init_crs_from_srid(pl_Crs* crs, std::string_view srid_string)
{
    auto code = get_epsg_code(srid_string);
    if (code.has_value())
    {
        pl_CrsDatabaseResult res = pl_get_crs(CRS_DB.get(), "EPSG", code.value(), crs);
        return res == pl_CrsDatabaseResult_Ok;
    }

    return false;
}

bool has_known_authority(std::string_view descriptor)
{
    return hrz::str::starts_with(descriptor, "EPSG:") || hrz::str::starts_with(descriptor, "OGC:")
        || hrz::str::starts_with(descriptor, "CRS:") || hrz::str::starts_with(descriptor, "OSGEO:")
        || hrz::str::starts_with(descriptor, "urn:ogc:def:crs:");
}

} // namespace

namespace hrz
{
bool convert_crs(
    std::string_view descriptor,
    hrz_proto::SrsDescriptorType descriptor_type,
    pl_Crs* crs)
{
    switch (descriptor_type)
    {
        case hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR:
            return init_crs_from_proj4_string(crs, descriptor);
        case hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR:
            return init_crs_from_srid(crs, descriptor);
        default: assert(false && "Unhandled case");
    }

    return false;
}

bool convert_crs(std::string_view descriptor, pl_Crs* crs)
{
    if (has_known_authority(descriptor))
    {
        return convert_crs(descriptor, hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, crs);
    }
    else
    {
        return convert_crs(descriptor, hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR, crs);
    }
}

bool convert_crs(const hrz_proto::SpatialReferenceSystem& srs, pl_Crs* crs)
{
    return convert_crs(srs.descriptor(), srs.descriptor_type(), crs);
}

bool convert_crs(std::string_view descriptor, hrz_proto::SpatialReferenceSystem* proto_srs)
{
    if (has_known_authority(descriptor))
    {
        proto_srs->set_descriptor_type(hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR);
        proto_srs->set_descriptor(descriptor.data(), descriptor.size());
    }
    else
    {
        proto_srs->set_descriptor_type(hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR);
        proto_srs->set_descriptor(descriptor.data(), descriptor.size());
    }

    // We still return whether we know the descriptor or not.
    pl_Crs crs;
    return convert_crs(*proto_srs, &crs);
}

bool check_crs(const hrz_proto::SpatialReferenceSystem& srs)
{
    pl_Crs crs;
    return convert_crs(srs.descriptor(), srs.descriptor_type(), &crs);
}

std::string srid_descriptor_to_proj4(std::string_view srid_string)
{
    auto code = get_epsg_code(srid_string);

    if (!code.has_value()) return "";

    size_t length = 0;
    pl_CrsDatabaseResult res =
        pl_get_crs_proj_str_length(CRS_DB.get(), "EPSG", code.value(), &length);
    if (res != pl_CrsDatabaseResult_Ok) return "";

    std::string proj_string;
    proj_string.resize(length);

    res = pl_get_crs_proj_str(
        CRS_DB.get(), "EPSG", code.value(), proj_string.data(), &length, length + 1);
    if (res != pl_CrsDatabaseResult_Ok) return "";

    return proj_string;
}
} // namespace hrz
