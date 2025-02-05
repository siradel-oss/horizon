#include "hrz_protocol_image_helper.h"

namespace hrz_proto
{
unsigned int channel_count(ImageFormat format)
{
    switch (format)
    {
        case ImageFormat::SRGBA_8: return 4;
        case ImageFormat::SIGNED_FIXED_24_8:
        case ImageFormat::MAPZEN_TERRARIUM:
        case ImageFormat::R_F32:
        case ImageFormat::R_F32_SILICIUM:
        case ImageFormat::SRGB_R_8: return 1;
        default:
        {
            assert(!"Unhandled image format in channel_count");
            return 1;
        }
    }
}

unsigned int bit_count(ImageFormat format)
{
    switch (format)
    {
        case ImageFormat::SRGBA_8: return 32;
        case ImageFormat::SIGNED_FIXED_24_8:
        case ImageFormat::MAPZEN_TERRARIUM:
        case ImageFormat::R_F32:
        case ImageFormat::R_F32_SILICIUM: return 32;
        case ImageFormat::SRGB_R_8: return 8;
        default:
        {
            assert(!"Unhandled image format in bit_count");
            return 8;
        }
    }
}

unsigned int byte_count(ImageFormat format)
{
    return bit_count(format) / 8;
}
} // namespace hrz_proto
