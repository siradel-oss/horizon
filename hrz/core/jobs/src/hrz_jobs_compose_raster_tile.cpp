#include "hrz_jobs_clipping.h"
#include "hrz_jobs_declarations.h"
#include "hrz_jobs_rasterizer.h"

#include <hrz_common_blob_allocator.h>
#include <hrz_common_fmt.h>
#include <hrz_common_geo.h>
#include <hrz_common_image_view.h>
#include <hrz_common_planet.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proto_maths.h>
#include <hrz_common_raster_sampling.h>
#include <hrz_common_tile_coords.h>
#include <hrz_fnd_log.h>

#include <vector>

using namespace hrz;
using namespace hrz::sampling;

namespace hrz_jobs::compose_raster_tile
{
struct Bounds
{
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::min();

    double (*pixel_to_value)(const void* pixel);

    template<typename ChannelType>
    void update(ChannelType* pixel)
    {
        double value = pixel_to_value(static_cast<void*>(pixel));
        min = std::min(min, value);
        max = std::max(max, value);
    }
};

namespace
{
template<typename ChannelType, unsigned int CHANNELS>
struct SamplingBlendingFunctionAdapter : public rasterizer::SampleAndComposeFunction<lm::vec2>
{
    SamplingBlendingFunctionAdapter(
        const ImageView& input,
        SamplingFunction* sampling_function,
        BlendingFunction* blending_function,
        std::optional<Bounds>& bounds) :
        input(input),
        sampling_function(sampling_function),
        blending_function(blending_function),
        bounds(bounds)
    {
    }

    HRZ_DELETE_COPY_MOVE(SamplingBlendingFunctionAdapter);
    ~SamplingBlendingFunctionAdapter() override = default;

    bool sample_and_compose(int pixel_x, int pixel_y, lm::vec2 uv, MutImageView& output) override
    {
        auto* data = output.pixel_data<ChannelType, CHANNELS>(pixel_x, pixel_y);

        bool write = sample_and_compose_raster<ChannelType, CHANNELS>(
            input, uv, sampling_function, blending_function, data);

        if (bounds.has_value() && write)
        {
            bounds->update(data);
        }

        return write;
    }

    const ImageView& input;
    SamplingFunction* sampling_function;
    BlendingFunction* blending_function;
    std::optional<Bounds>& bounds;
};

void _rasterize_tile(
    const ImageView& input,
    MutImageView& output,
    std::span<const float> coords,
    unsigned int quad_count_x,
    unsigned int quad_count_y,
    lm::bbox2 clip_uv,
    const lm::bbox2& dst_clip_uv,
    SamplingFunction* sampling_function,
    BlendingFunction* blending_function,
    std::optional<Bounds>& bounds,
    const TileCoords& tile_coords,
    const TileCoords& tile_image_coords)
{
    HRZ_SCOPED_SAMPLE_A("rasterize tile");

    TileToTileUvTransform<float> uv_xform(tile_coords, tile_image_coords);

    float uv_step_x = 1.0 / quad_count_x;
    float uv_step_y = 1.0 / quad_count_y;

    assert(coords.size() == (quad_count_x + 1) * (quad_count_y + 1) * 2);
    if (coords.size() != (quad_count_x + 1) * (quad_count_y + 1) * 2) return;

    std::unique_ptr<rasterizer::SampleAndComposeFunction<lm::vec2>> sample_and_compose_function;
    switch (input.format)
    {
        case hrz_proto::ImageFormat::SRGBA_8:
            sample_and_compose_function =
                std::make_unique<SamplingBlendingFunctionAdapter<uint8_t, 4>>(
                    input, sampling_function, blending_function, bounds);
            break;
        case hrz_proto::ImageFormat::R_F32:
        case hrz_proto::ImageFormat::R_F32_SILICIUM:
        case hrz_proto::ImageFormat::TERRARIUM:
        case hrz_proto::ImageFormat::TERRAIN_RGB:
            sample_and_compose_function =
                std::make_unique<SamplingBlendingFunctionAdapter<float, 1>>(
                    input, sampling_function, blending_function, bounds);
            break;
        case hrz_proto::ImageFormat::SIGNED_FIXED_24_8:
            sample_and_compose_function =
                std::make_unique<SamplingBlendingFunctionAdapter<int32_t, 1>>(
                    input, sampling_function, blending_function, bounds);
            break;
        default: assert(false && "Unhandled image format"); break;
    }

    auto rasterize_function = rasterizer::rasterize_triangle<lm::vec2>;

    clip_uv.min = uv_xform(clip_uv.min);
    clip_uv.max = uv_xform(clip_uv.max);

    for (unsigned int y = 0; y < quad_count_y; ++y)
    {
        for (unsigned int x = 0; x < quad_count_x; ++x)
        {
            auto p00_ptr = coords.data() + (x + (y + 0) * (quad_count_x + 1)) * 2;
            auto p01_ptr = coords.data() + (x + (y + 1) * (quad_count_x + 1)) * 2;

            lm::vec2 p00(p00_ptr[0], p00_ptr[1]);
            lm::vec2 p10(p00_ptr[2], p00_ptr[3]);
            lm::vec2 p01(p01_ptr[0], p01_ptr[1]);
            lm::vec2 p11(p01_ptr[2], p01_ptr[3]);

            lm::vec2 uv00((x + 0) * uv_step_x, (y + 0) * uv_step_y);
            lm::vec2 uv10((x + 1) * uv_step_x, (y + 0) * uv_step_y);
            lm::vec2 uv01((x + 0) * uv_step_x, (y + 1) * uv_step_y);
            lm::vec2 uv11((x + 1) * uv_step_x, (y + 1) * uv_step_y);

            uv00 = uv_xform(uv00);
            uv10 = uv_xform(uv10);
            uv01 = uv_xform(uv01);
            uv11 = uv_xform(uv11);

            hrz::clip_triangle(
                p00, p01, p10, uv00, uv01, uv10, dst_clip_uv,
                [&](lm::vec2 pa, lm::vec2 pb, lm::vec2 pc, lm::vec2 uva, lm::vec2 uvb, lm::vec2 uvc)
                {
                    hrz::clip_triangle(
                        uva, uvb, uvc, pa, pb, pc, clip_uv,
                        [&](lm::vec2 uva, lm::vec2 uvb, lm::vec2 uvc, lm::vec2 pa, lm::vec2 pb,
                            lm::vec2 pc) {
                            rasterize_function(
                                output, pa, pb, pc, uva, uvb, uvc,
                                sample_and_compose_function.get());
                        });
                });

            hrz::clip_triangle(
                p10, p01, p11, uv10, uv01, uv11, dst_clip_uv,
                [&](lm::vec2 pa, lm::vec2 pb, lm::vec2 pc, lm::vec2 uva, lm::vec2 uvb, lm::vec2 uvc)
                {
                    hrz::clip_triangle(
                        uva, uvb, uvc, pa, pb, pc, clip_uv,
                        [&](lm::vec2 uva, lm::vec2 uvb, lm::vec2 uvc, lm::vec2 pa, lm::vec2 pb,
                            lm::vec2 pc) {
                            rasterize_function(
                                output, pa, pb, pc, uva, uvb, uvc,
                                sample_and_compose_function.get());
                        });
                });
        }
    }
}

struct SubSamplingParams
{
    int32_t out_x;
    int32_t out_y;
    uint32_t in_width;
    uint32_t in_height;
    int32_t in_x;
    int32_t in_y;
    uint32_t pixel_size;
};

template<typename T>
struct SubSamplingTraits
{
};

template<>
struct SubSamplingTraits<uint8_t>
{
    using AccumulationType = uint64_t;
};

template<>
struct SubSamplingTraits<int32_t>
{
    using AccumulationType = int64_t;
};

template<>
struct SubSamplingTraits<float>
{
    using AccumulationType = double;
};

template<typename T, unsigned int CHANNELS>
inline void _subsample_tile(
    const ImageView& input,
    MutImageView& output,
    const SubSamplingParams& params,
    SamplingFunction* sampling_function,
    BlendingFunction* blending_function,
    std::optional<Bounds>& bounds)
{
    using AccType = typename SubSamplingTraits<T>::AccumulationType;

    T* out_data = output.pixel_data<T, CHANNELS>(params.out_x, params.out_y);

    // This is the base color of the image before we blit the tile we're
    // subsampling. We must blend every subsampled pixel with this to get the
    // correct color, and then we'll copy this back to the output image.
    std::array<T, CHANNELS> base_data;
    memcpy(base_data.data(), out_data, CHANNELS * sizeof(T));

    std::array<AccType, CHANNELS> mean;
    mean.fill(0);

    for (uint32_t i = 0; i < params.pixel_size; i++)
    {
        for (uint32_t j = 0; j < params.pixel_size; j++)
        {
            std::array<T, CHANNELS> value = base_data;

            lm::vec2 uv(
                (params.in_x + j) / (float)params.in_width,
                (params.in_y + i) / (float)params.in_height);

            sample_and_compose_raster<T, CHANNELS>(
                input, uv, sampling_function, blending_function, value.data());

            for (uint32_t c = 0; c < CHANNELS; ++c)
            {
                mean[c] += (AccType)value[c];
            }
        }
    }

    AccType pixel_count = (AccType)params.pixel_size * (AccType)params.pixel_size;

    std::array<T, CHANNELS> final_data;
    for (uint32_t c = 0; c < CHANNELS; ++c)
    {
        final_data[c] = clamp_cast<AccType, T>(mean[c] / pixel_count);
    }

    memcpy(out_data, final_data.data(), CHANNELS * sizeof(T));

    if (bounds.has_value())
    {
        bounds->update(out_data);
    }
}

/**
 * This function aims to blit input tile image pixels intersecting output tile image pixels.
 * Output images is slightly larger than input image for filtering purpose, therefore we had to know
 * if the output image borders are covered by all input images at the beginning of this function.
 *
 * 3 main cases need to be adressed :
 *   - input tile zoom level is equal the output tile zoom level : in this case we simply copy
 * pixels intersecting or mirror the corresponding output tile image pixel if the border is not
 * covered.
 *   - input tile zoom level is higher than output tile zoom level : we take all input tile image
 * pixels covered by one pixel of the output tile image and mean the values. Again if a border is
 * not covered we mirror the corresponding output tile image pixel.
 *   - input tile zoom level is lower than output tile zoom level : we take all ouput tile image
 * pixels covered by one pixel of the input tile image and copy the value for all of them. Again if
 * a border is not covered we mirror the corresponding output tile image pixel.
 */
template<typename T, unsigned int CHANNELS>
void _blit_tile(
    const ImageView& input,
    MutImageView& output,
    const hrz::TileCoords& in_tile_coords,
    const hrz::TileCoords& out_tile_coords,
    lm::bbox2 clip_uv,
    uint8_t covered_borders,
    SamplingFunction* sampling_function,
    BlendingFunction* blending_function,
    std::optional<Bounds>& bounds)
{
    //  0 | 1 | 2
    // ---|---|---
    //  3 |   | 4
    // ---|---|---
    //  5 | 6 | 7

    const uint8_t top_mask = 0x7;     // 00000111
    const uint8_t bottom_mask = 0xE0; // 11100000
    const uint8_t right_mask = 0x94;  // 10010100
    const uint8_t left_mask = 0x29;   // 00101001

    // We test the top-left corner of pixels against clip_uv.
    // To avoid getting void pixels because some data bounds somewhere is very very
    // slightly wrong, we add a small padding to clip_uv of half a pixel.
    static constexpr double CLIP_PADDING = 0.5 / (double)MERCATOR_TILE_SIZE;
    clip_uv.min.x -= CLIP_PADDING;
    clip_uv.min.y -= CLIP_PADDING;
    clip_uv.max.x += CLIP_PADDING;
    clip_uv.max.y += CLIP_PADDING;

    assert(
        input.width == MERCATOR_TILE_SIZE && input.height == MERCATOR_TILE_SIZE
        && output.width == ATLAS_TILE_SIZE && output.height == ATLAS_TILE_SIZE);

    if (in_tile_coords == out_tile_coords)
    {
        for (int32_t y = 0; y < ATLAS_TILE_SIZE; y++)
        {
            uint8_t row_pixel_mask = 0xFF;
            row_pixel_mask &= (y < ATLAS_TILE_BORDER_SIZE) ? top_mask : ~top_mask;
            row_pixel_mask &=
                (y >= ATLAS_TILE_SIZE - ATLAS_TILE_BORDER_SIZE) ? bottom_mask : ~bottom_mask;

            int32_t in_y =
                std::min(std::max(0, y - ATLAS_TILE_BORDER_SIZE), MERCATOR_TILE_SIZE - 1);
            float uv_y = (float)in_y / MERCATOR_TILE_SIZE;

            if (uv_y < clip_uv.min.y || uv_y > clip_uv.max.y) continue;

            for (int32_t x = 0; x < ATLAS_TILE_SIZE; x++)
            {
                uint8_t pixel_mask = row_pixel_mask;
                pixel_mask &= (x < ATLAS_TILE_BORDER_SIZE) ? left_mask : ~left_mask;
                pixel_mask &=
                    (x >= ATLAS_TILE_SIZE - ATLAS_TILE_BORDER_SIZE) ? right_mask : ~right_mask;
                if (pixel_mask & covered_borders) continue;

                int32_t in_x =
                    std::min(std::max(0, x - ATLAS_TILE_BORDER_SIZE), MERCATOR_TILE_SIZE - 1);
                float uv_x = (float)in_x / MERCATOR_TILE_SIZE;

                if (uv_x < clip_uv.min.x || uv_x > clip_uv.max.x) continue;

                T* out_data = output.pixel_data<T, CHANNELS>(x, y);

                bool write = sample_and_compose_raster<T, CHANNELS>(
                    input, lm::vec2(uv_x, uv_y), sampling_function, blending_function, out_data);

                if (bounds.has_value() && write)
                {
                    bounds->update(out_data);
                }
            }
        }
    }
    else if (in_tile_coords.lod == out_tile_coords.lod)
    {
        int32_t out_x0 =
            ((int32_t)out_tile_coords.x - (int32_t)in_tile_coords.x) * MERCATOR_TILE_SIZE;
        int32_t out_y0 =
            ((int32_t)out_tile_coords.y - (int32_t)in_tile_coords.y) * MERCATOR_TILE_SIZE;

        for (int32_t y = 0; y < ATLAS_TILE_SIZE; y++)
        {
            int32_t in_y = out_y0 + y - ATLAS_TILE_BORDER_SIZE;
            if (in_y < 0 || in_y >= MERCATOR_TILE_SIZE) continue;

            float uv_y = (float)in_y / MERCATOR_TILE_SIZE;
            if (uv_y < clip_uv.min.y || uv_y > clip_uv.max.y) continue;

            for (int32_t x = 0; x < ATLAS_TILE_SIZE; x++)
            {
                int32_t in_x = out_x0 + x - ATLAS_TILE_BORDER_SIZE;
                if (in_x < 0 || in_x >= MERCATOR_TILE_SIZE) continue;

                float uv_x = (float)in_x / MERCATOR_TILE_SIZE;
                if (uv_x < clip_uv.min.x || uv_x > clip_uv.max.x) continue;

                T* out_data = output.pixel_data<T, CHANNELS>(x, y);

                bool write = sample_and_compose_raster<T, CHANNELS>(
                    input, lm::vec2(uv_x, uv_y), sampling_function, blending_function, out_data);

                if (bounds.has_value() && write)
                {
                    bounds->update(out_data);
                }
            }
        }
    }
    else if (in_tile_coords.lod > out_tile_coords.lod)
    {
        uint32_t lod_diff = in_tile_coords.lod - out_tile_coords.lod;
        int64_t pixel_size = 1 << lod_diff;

        // if one pixel is larger or equal than the entire input image just exit.
        if (pixel_size >= MERCATOR_TILE_SIZE) return;

        int64_t in_x0 = (int64_t)in_tile_coords.x * MERCATOR_TILE_SIZE;
        int64_t in_y0 = (int64_t)in_tile_coords.y * MERCATOR_TILE_SIZE;

        int64_t out_x0 = (int64_t)out_tile_coords.x * pixel_size * MERCATOR_TILE_SIZE;
        int64_t out_y0 = (int64_t)out_tile_coords.y * pixel_size * MERCATOR_TILE_SIZE;

        int32_t off_x0 = (int32_t)out_x0 - (int32_t)in_x0;
        int32_t off_y0 = (int32_t)out_y0 - (int32_t)in_y0;

        bool is_inside = (in_tile_coords.x >> lod_diff == out_tile_coords.x)
            && (in_tile_coords.y >> lod_diff == out_tile_coords.y);
        if (is_inside)
        {
            for (int64_t y = 0; y < ATLAS_TILE_SIZE; y++)
            {
                uint8_t row_pixel_mask = 0xFF;
                row_pixel_mask &= (y == 0) ? top_mask : ~top_mask;
                row_pixel_mask &= (y == ATLAS_TILE_SIZE - 1) ? bottom_mask : ~bottom_mask;

                int64_t off_y = std::min<int64_t>(
                    std::max<int64_t>(0, y - ATLAS_TILE_BORDER_SIZE), MERCATOR_TILE_SIZE - 1);

                int64_t in_y = off_y0 + off_y * pixel_size;
                if (in_y < 0 || in_y > MERCATOR_TILE_SIZE - (int)pixel_size) continue;

                float uv_y = (float)in_y / MERCATOR_TILE_SIZE;
                if (uv_y < clip_uv.min.y || uv_y > clip_uv.max.y) continue;

                for (int64_t x = 0; x < ATLAS_TILE_SIZE; x++)
                {
                    uint8_t pixel_mask = row_pixel_mask;
                    pixel_mask &= (x == 0) ? left_mask : ~left_mask;
                    pixel_mask &= (x == ATLAS_TILE_SIZE - 1) ? right_mask : ~right_mask;
                    if (pixel_mask & covered_borders) continue;

                    int64_t off_x = std::min<int64_t>(
                        std::max<int64_t>(0, x - ATLAS_TILE_BORDER_SIZE), MERCATOR_TILE_SIZE - 1);
                    int64_t in_x = off_x0 + off_x * pixel_size;

                    if (in_x < 0 || in_x > (int32_t)(MERCATOR_TILE_SIZE - pixel_size)) continue;

                    float uv_x = (float)in_x / MERCATOR_TILE_SIZE;
                    if (uv_x < clip_uv.min.x || uv_x > clip_uv.max.x) continue;

                    SubSamplingParams params = {(int32_t)x,
                                                (int32_t)y,
                                                (uint32_t)MERCATOR_TILE_SIZE,
                                                (uint32_t)MERCATOR_TILE_SIZE,
                                                (int32_t)in_x,
                                                (int32_t)in_y,
                                                (uint32_t)pixel_size};
                    _subsample_tile<T, CHANNELS>(
                        input, output, params, sampling_function, blending_function, bounds);
                }
            }
        }
        else
        {
            for (int32_t y = 0; y < ATLAS_TILE_SIZE; y++)
            {
                int32_t in_y = off_y0 + (y - ATLAS_TILE_BORDER_SIZE) * pixel_size;
                if (in_y < 0 || in_y > (MERCATOR_TILE_SIZE - (int)pixel_size)) continue;

                float uv_y = (float)in_y / MERCATOR_TILE_SIZE;
                if (uv_y < clip_uv.min.y || uv_y > clip_uv.max.y) continue;

                for (int32_t x = 0; x < ATLAS_TILE_SIZE; x++)
                {
                    int32_t in_x = off_x0 + (x - ATLAS_TILE_BORDER_SIZE) * pixel_size;
                    if (in_x < 0 || in_x > (int32_t)(MERCATOR_TILE_SIZE - pixel_size)) continue;

                    float uv_x = (float)in_x / MERCATOR_TILE_SIZE;
                    if (uv_x < clip_uv.min.x || uv_x > clip_uv.max.x) continue;

                    SubSamplingParams params = {(int32_t)x,
                                                (int32_t)y,
                                                (uint32_t)MERCATOR_TILE_SIZE,
                                                (uint32_t)MERCATOR_TILE_SIZE,
                                                (int32_t)in_x,
                                                (int32_t)in_y,
                                                (uint32_t)pixel_size};
                    _subsample_tile<T, CHANNELS>(
                        input, output, params, sampling_function, blending_function, bounds);
                }
            }
        }
    }
    else
    {
        int32_t lod_diff = (out_tile_coords.lod - in_tile_coords.lod);
        int64_t pixel_size = 1 << lod_diff;

        int64_t out_x0 = (int64_t)out_tile_coords.x * MERCATOR_TILE_SIZE;
        int64_t out_y0 = (int64_t)out_tile_coords.y * MERCATOR_TILE_SIZE;

        int64_t in_x0 = (int64_t)in_tile_coords.x * pixel_size * MERCATOR_TILE_SIZE;
        int64_t in_y0 = (int64_t)in_tile_coords.y * pixel_size * MERCATOR_TILE_SIZE;

        int64_t off_x0 = out_x0 - in_x0;
        int64_t off_y0 = out_y0 - in_y0;

        bool is_inside = (out_tile_coords.x >> lod_diff == in_tile_coords.x)
            && (out_tile_coords.y >> lod_diff == in_tile_coords.y);
        if (is_inside)
        {
            uint32_t num_tiles = 1 << lod_diff;
            uint8_t covered_inside_borders = 0x00;
            for (int i = 0; i < (int)num_tiles; i++)
            {
                int32_t y = (in_tile_coords.y << lod_diff) + i - out_tile_coords.y;
                if (std::abs(y) > 1) continue;

                for (int j = 0; j < (int)num_tiles; j++)
                {
                    int32_t x = (in_tile_coords.x << lod_diff) + j - out_tile_coords.x;
                    if (std::abs(x) > 1 || (std::abs(x) == 0 && std::abs(y) == 0)) continue;

                    int index = (y + 1) * 3 + x + 1;
                    if (index > 4) --index;
                    covered_inside_borders |= 1 << index;
                }
            }

            uint8_t covered_outside_borders = covered_borders & ~covered_inside_borders;

            for (int64_t y = 0; y < ATLAS_TILE_SIZE; y++)
            {
                uint8_t row_pixel_mask = 0xFF;
                bool top = y == 0;
                bool bottom = y == ATLAS_TILE_SIZE - 1;
                row_pixel_mask &= top ? top_mask : ~top_mask;
                row_pixel_mask &= bottom ? bottom_mask : ~bottom_mask;

                // Skip if whole row is filled
                if (top && (top_mask & covered_outside_borders) == top_mask) continue;
                if (bottom && (bottom_mask & covered_outside_borders) == bottom_mask) continue;

                for (int64_t x = 0; x < ATLAS_TILE_SIZE; x++)
                {
                    uint8_t pixel_mask = row_pixel_mask;
                    bool left = x == 0;
                    bool right = x == ATLAS_TILE_SIZE - 1;

                    pixel_mask &= left ? left_mask : ~left_mask;
                    pixel_mask &= right ? right_mask : ~right_mask;

                    // Skip of pixel is already filled
                    if (pixel_mask & covered_outside_borders) continue;

                    bool non_covered_pixel = !(pixel_mask & covered_borders);

                    // Extent the tile to fill in the padding
                    int64_t off_x = -ATLAS_TILE_BORDER_SIZE;
                    int64_t off_y = -ATLAS_TILE_BORDER_SIZE;
                    if (non_covered_pixel)
                    {
                        if (left)
                            off_x += ATLAS_TILE_BORDER_SIZE;
                        else if (right)
                            off_x -= ATLAS_TILE_BORDER_SIZE;

                        if (top)
                            off_y += ATLAS_TILE_BORDER_SIZE;
                        else if (bottom)
                            off_y -= ATLAS_TILE_BORDER_SIZE;
                    }

                    float in_x = (float)(off_x0 + x + off_x) / pixel_size / MERCATOR_TILE_SIZE;
                    float in_y = (float)(off_y0 + y + off_y) / pixel_size / MERCATOR_TILE_SIZE;
                    lm::vec2 uv(in_x, in_y);

                    if (in_y < 0 || in_y >= 1 || in_x < 0 || in_x >= 1
                        || !lm::contains(clip_uv, uv))
                        continue;

                    T* out_data = output.pixel_data<T, CHANNELS>(x, y);
                    bool write = sample_and_compose_raster<T, CHANNELS>(
                        input, uv, sampling_function, blending_function, out_data);

                    if (bounds.has_value() && write)
                    {
                        bounds->update(out_data);
                    }
                }
            }
        }
        else
        {
            for (int32_t y = 0; y < ATLAS_TILE_SIZE; y++)
            {
                float in_y = (float)(off_y0 + (y - ATLAS_TILE_BORDER_SIZE)) / pixel_size
                    / MERCATOR_TILE_SIZE;
                if (in_y < 0 || in_y >= 1 || in_y < clip_uv.min.y || in_y > clip_uv.max.y) continue;

                for (int32_t x = 0; x < ATLAS_TILE_SIZE; x++)
                {
                    float in_x = (float)(off_x0 + (x - ATLAS_TILE_BORDER_SIZE)) / pixel_size
                        / MERCATOR_TILE_SIZE;
                    if (in_x < 0 || in_x >= 1 || in_x < clip_uv.min.x || in_x > clip_uv.max.x)
                        continue;

                    T* out_data = output.pixel_data<T, CHANNELS>(x, y);
                    bool write = sample_and_compose_raster<T, CHANNELS>(
                        input, lm::vec2(in_x, in_y), sampling_function, blending_function,
                        out_data);

                    if (bounds.has_value() && write)
                    {
                        bounds->update(out_data);
                    }
                }
            }
        }
    }
}

std::unique_ptr<BlendingFunction> _make_blending_function(
    const uint8_t opacity,
    hrz_proto::ImageFormat image_format)
{
    if (image_format == hrz_proto::ImageFormat::SRGBA_8)
    {
        return std::unique_ptr<BlendingFunction>(new ImageryBlendingFunction(opacity));
    }
    else if (image_format == hrz_proto::ImageFormat::R_F32)
    {
        return std::unique_ptr<BlendingFunction>(new DtmBlendingFunction(opacity));
    }
    else
    {
        HRZ_LOG_ERROR("Unsupported image format: {}", hrz_proto::ImageFormat_Name(image_format));
        assert(false);
        return nullptr;
    }
}
} // namespace

hrz::JobResult run(
    const hrz::planet::RasterTileCompositionParams& params,
    hrz::planet::RasterTileCompositionResponse& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("compose tile job");

    size_t output_image_data_size = hrz::ATLAS_TILE_SIZE * hrz::ATLAS_TILE_SIZE
        * hrz::image_format_byte_count(params.output_format);
    auto output_image_blob =
        hrz::blobs::allocate_blob_sync(context.get_blob_allocator(), output_image_data_size);
    if (!output_image_blob.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate output image");
        return hrz::JobResult::FAILURE;
    }

    blobs::register_owner(
        context.get_blob_allocator(), output_image_blob.value(), context.get_resource_owner());
    auto output_image_data = output_image_blob->get_mutable_data();

    std::memset(output_image_data.data(), 0, output_image_data_size);

    MutImageView output_image(
        output_image_data, params.output_format, hrz::ATLAS_TILE_SIZE, hrz::ATLAS_TILE_SIZE);

    // Check if mirrored borders are needed for output tiles.
    //  0  1  2
    //   *---*
    //  3|   |4  indices are arranged that way.
    //   *---*
    //  5  6  7
    uint8_t covered_borders = 0x00;
    for (unsigned int i = 0; i < params.images.size(); i++)
    {
        const auto& img_canvas = params.images.at(i);

        if (!std::holds_alternative<hrz::planet::RasterTileCompositionParams::Blit>(
                img_canvas.canvas))
        {
            continue;
        }

        const auto& blit =
            std::get<hrz::planet::RasterTileCompositionParams::Blit>(img_canvas.canvas);
        int lod_diff = (int)blit.input_coords.lod - (int)params.output_coords.lod;

        if (lod_diff >= 0)
        {
            auto in_coords =
                std::get<hrz::planet::RasterTileCompositionParams::Blit>(img_canvas.canvas)
                    .input_coords;
            int x = (int)(in_coords.x >> lod_diff) - (int)params.output_coords.x;
            int y = (int)(in_coords.y >> lod_diff) - (int)params.output_coords.y;

            if (std::abs(x) > 1 || std::abs(y) > 1 || (std::abs(x) == 0 && std::abs(y) == 0))
            {
                continue;
            }

            int index = (y + 1) * 3 + x + 1;
            if (index > 4) --index;

            covered_borders |= 1 << index;
        }
        else
        {
            const auto& in_coords =
                std::get<hrz::planet::RasterTileCompositionParams::Blit>(img_canvas.canvas)
                    .input_coords;

            int out_x = params.output_coords.x;
            int out_y = params.output_coords.y;

            int in_min_x = (int)in_coords.x << -lod_diff;
            int in_min_y = (int)in_coords.y << -lod_diff;

            int in_max_x = in_min_x + (1 << -lod_diff) - 1;
            int in_max_y = in_min_y + (1 << -lod_diff) - 1;

            lm::ibbox2 in_bbox{{in_min_x, in_min_y}, {in_max_x, in_max_y}};

            covered_borders |= lm::contains(in_bbox, lm::ivec2{out_x - 1, out_y - 1}) ? 1 << 0 : 0;
            covered_borders |= lm::contains(in_bbox, lm::ivec2{out_x + 0, out_y - 1}) ? 1 << 1 : 0;
            covered_borders |= lm::contains(in_bbox, lm::ivec2{out_x + 1, out_y - 1}) ? 1 << 2 : 0;
            covered_borders |= lm::contains(in_bbox, lm::ivec2{out_x - 1, out_y + 0}) ? 1 << 3 : 0;
            covered_borders |= lm::contains(in_bbox, lm::ivec2{out_x + 1, out_y + 0}) ? 1 << 4 : 0;
            covered_borders |= lm::contains(in_bbox, lm::ivec2{out_x - 1, out_y + 1}) ? 1 << 5 : 0;
            covered_borders |= lm::contains(in_bbox, lm::ivec2{out_x + 0, out_y + 1}) ? 1 << 6 : 0;
            covered_borders |= lm::contains(in_bbox, lm::ivec2{out_x + 1, out_y + 1}) ? 1 << 7 : 0;
        }
    }

    std::optional<Bounds> bounds = std::nullopt;
    if (params.compute_value_bounds)
    {
        bounds.emplace();
        bounds->pixel_to_value = params.pixel_to_value;
    }

    for (const auto& img_canvas : params.images)
    {
        auto input_image_data = img_canvas.image.data();

        auto image_format_opt = img_canvas.image.proto_format();
        if (!image_format_opt.has_value())
        {
            assert(false && "Unsupported image format");
            break;
        }
        auto image_format = image_format_opt.value();

        ImageView input_image(
            input_image_data, image_format, img_canvas.image.width(), img_canvas.image.height());

        const auto& nodata = img_canvas.nodata;
        const auto& sampling = img_canvas.sampling;
        const auto& blending = img_canvas.blending;

        auto blending_function =
            _make_blending_function(std::round(blending.opacity() * 255), params.output_format);

        if (std::holds_alternative<hrz::planet::RasterTileCompositionParams::ReprojectionMesh>(
                img_canvas.canvas))
        {
            auto sampling_function = hrz::sampling::make_sampling_function(
                sampling.alpha_channel_usage(), nodata, sampling.nodata_handling(),
                sampling.filtering(), image_format);

            auto& reprojection_mesh =
                std::get<hrz::planet::RasterTileCompositionParams::ReprojectionMesh>(
                    img_canvas.canvas);
            _rasterize_tile(
                input_image, output_image, reprojection_mesh.grid, reprojection_mesh.quad_count.x,
                reprojection_mesh.quad_count.y, reprojection_mesh.uv_clip, img_canvas.dst_uv_clip,
                sampling_function.get(), blending_function.get(), bounds,
                reprojection_mesh.tile_coords, reprojection_mesh.tile_image_coords);
        }
        else
        {
            auto& blit =
                std::get<hrz::planet::RasterTileCompositionParams::Blit>(img_canvas.canvas);

            // we don't need bilinear filtering when dealing same lod blitting.
            auto lod_diff = blit.input_coords.lod - params.output_coords.lod;
            auto filtering =
                lod_diff == 0 ? HrzProtocol::TextureFiltering::NEAREST : sampling.filtering();
            auto sampling_function = hrz::sampling::make_sampling_function(
                sampling.alpha_channel_usage(), nodata, sampling.nodata_handling(), filtering,
                image_format);

            decltype(&_blit_tile<uint8_t, 4>) blit_function = nullptr;
            switch (params.output_format)
            {
                case hrz_proto::ImageFormat::SRGBA_8: blit_function = _blit_tile<uint8_t, 4>; break;
                case hrz_proto::ImageFormat::R_F32: blit_function = _blit_tile<float, 1>; break;
                default: assert(false && "Unhandled image format"); break;
            }

            // No need to deal with dst clip UV when blitting images, they have already been
            // baked in the UV clip values in the reprojection job.

            blit_function(
                input_image, output_image, blit.input_coords, params.output_coords, blit.uv_clip,
                covered_borders, sampling_function.get(), blending_function.get(), bounds);
        }
    }

    output_image_data.release();

    response.image = hrz::BlobImage::make(
        output_image.format, output_image.width, output_image.height,
        std::move(output_image_blob.value()), context.get_blob_allocator());

    if (bounds.has_value())
    {
        if (bounds->min <= bounds->max)
        {
            response.min_value = bounds->min;
            response.max_value = bounds->max;
        }
        else
        {
            // There were no pixels written to the output.
            response.min_value = 0.0;
            response.max_value = 0.0;
        }
    }

    return hrz::JobResult::SUCCESS;
}

} // namespace hrz_jobs::compose_raster_tile
