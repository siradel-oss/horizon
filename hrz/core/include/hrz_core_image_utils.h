#pragma once

#include "hrz_fnd_log.h"
#include "hrz_protocol_all.h"

#include <mycelium_backend.h>

namespace hrz
{
inline my::TextureFormat image_format_to_gpu_format(hrz_proto::ImageFormat format)
{
    switch (format)
    {
        case hrz_proto::ImageFormat::SRGBA_8: return my::TextureFormat::RGBA8;
        case hrz_proto::ImageFormat::SIGNED_FIXED_24_8:
        case hrz_proto::ImageFormat::MAPZEN_TERRARIUM: return my::TextureFormat::R32I;
        case hrz_proto::ImageFormat::R_F32:
        case hrz_proto::ImageFormat::R_F32_SILICIUM: return my::TextureFormat::R32F;
        default:
            HRZ_LOG_ERROR("Unhandled image format: {}", hrz_proto::ImageFormat_Name(format));
            assert(false);
            return my::TextureFormat::RGBA8;
    }
}

inline hrz_proto::ImageFormat gpu_format_to_image_format(my::TextureFormat format)
{
    switch (format)
    {
        case my::TextureFormat::RGBA8: return hrz_proto::ImageFormat::SRGBA_8;
        case my::TextureFormat::R32I: return hrz_proto::ImageFormat::SIGNED_FIXED_24_8;
        case my::TextureFormat::R32F: return hrz_proto::ImageFormat::R_F32;
        default:
            HRZ_LOG_ERROR("Unhandled gpu format: {}", (int)format);
            assert(false);
            return hrz_proto::ImageFormat::SRGBA_8;
    }
}
} // namespace hrz
