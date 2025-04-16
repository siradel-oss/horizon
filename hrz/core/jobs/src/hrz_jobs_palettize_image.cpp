#include "hrz_jobs_declarations.h"

#include <hrz_common_blob_allocator.h>
#include <hrz_common_image_processing.h>
#include <hrz_common_image_view.h>
#include <hrz_common_palette.h>
#include <hrz_common_planet.h>
#include <hrz_common_profiling.h>
#include <hrz_common_raster_sampling.h>
#include <hrz_fnd_log.h>

namespace hrz_jobs::palettize_image
{
hrz::JobResult run(
    const hrz::planet::PalettizeImageParams& params,
    hrz::BlobImage& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("palettize image job");

    if (!params.image.proto_format().has_value())
    {
        HRZ_LOG_ERROR("Unsupported image format");
        assert(false && "Unsupported image format");
        return hrz::JobResult::FAILURE;
    }
    auto image_format = params.image.proto_format().value();

    decltype(&hrz::sampling::fetch_r_f32_pixel) fetch_pixel_func;
    switch (image_format)
    {
        case hrz_proto::ImageFormat::R_F32:
            fetch_pixel_func = hrz::sampling::fetch_r_f32_pixel;
            break;
        case hrz_proto::ImageFormat::R_F32_SILICIUM:
            fetch_pixel_func = hrz::sampling::fetch_r_f32_silicium_pixel;
            break;
        case hrz_proto::ImageFormat::SIGNED_FIXED_24_8:
            fetch_pixel_func = hrz::sampling::fetch_signed_fixed_24_8_pixel;
            break;
        case hrz_proto::ImageFormat::TERRARIUM:
            fetch_pixel_func = hrz::sampling::fetch_terrarium_pixel;
            break;
        case hrz_proto::ImageFormat::TERRAIN_RGB:
            fetch_pixel_func = hrz::sampling::fetch_terrain_rgb_pixel;
            break;
        default:
            HRZ_LOG_ERROR(
                "Unsupported image format: {}", hrz_proto::ImageFormat_Name(image_format));
            assert(false && "Unsupported image format");
            return hrz::JobResult::FAILURE;
    }

    assert(my::format_channel_count(params.image.format()) == 1);

    hrz::sampling::NodataFunction nodata_function(
        params.nodata.value(),
        params.nodata.has_nodata() ? hrz_proto::NodataHandling::DISCARD_NODATA_PIXELS
                                   : hrz_proto::NodataHandling::IGNORE_NODATA,
        image_format);

    size_t output_image_data_size = params.image.width() * params.image.height()
        * hrz::image_format_byte_count(HrzProtocol::ImageFormat::SRGBA_8);
    auto output_image_blob_opt =
        hrz::blobs::allocate_blob_sync(context.get_blob_allocator(), output_image_data_size);
    if (!output_image_blob_opt.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate output image");
        return hrz::JobResult::FAILURE;
    }

    hrz::blobs::register_owner(
        context.get_blob_allocator(), output_image_blob_opt.value(), context.get_resource_owner());

    auto input_image_data = params.image.data();
    {
        hrz::ImageView image_view(
            input_image_data, image_format, params.image.width(), params.image.height());

        auto output_image_data = output_image_blob_opt->get_mutable_data();

        uint8_t* out_ptr = (uint8_t*)output_image_data.data();
        for (uint32_t y = 0; y < params.image.height(); ++y)
        {
            for (uint32_t x = 0; x < params.image.width(); ++x)
            {
                auto pixel = fetch_pixel_func(image_view, x, y, nodata_function);

                lm::vec4 color;
                if (!pixel.is_nodata)
                {
                    color =
                        hrz::palette::numeric_palettization(params.palette, pixel.value[0]).value();
                }
                else
                {
                    color = params.nodata_color;
                }

                *out_ptr++ = (uint8_t)(color.r * 255);
                *out_ptr++ = (uint8_t)(color.g * 255);
                *out_ptr++ = (uint8_t)(color.b * 255);
                *out_ptr++ = (uint8_t)(color.a * 255);
            }
        }
    }

    response = hrz::BlobImage::make(
        HrzProtocol::ImageFormat::SRGBA_8, params.image.width(), params.image.height(),
        std::move(output_image_blob_opt.value()), context.get_blob_allocator());

    return hrz::JobResult::SUCCESS;
}

} // namespace hrz_jobs::palettize_image
