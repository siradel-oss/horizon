// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/image_view.h"

#include <lin_maths.h>

namespace hrz_jobs::rasterizer
{
namespace
{

static constexpr int SUBPIXEL_PRECISION = 256;

inline bool _is_top_left_edge(lm::ivec2 a, lm::ivec2 b)
{
    return (a.y == b.y && b.x < a.x) || // top edge
        (a.y < b.y);                    // left edge
}

inline lm::ivec2 _weight_step(const lm::ivec2& a, const lm::ivec2& b)
{
    return lm::ivec2(a.y - b.y, b.x - a.x);
}

inline int _weight(const lm::ivec2& a, const lm::ivec2& b, const lm::ivec2& c, int bias)
{
    int64_t a_x = a.x;
    int64_t a_y = a.y;
    int64_t b_x = b.x;
    int64_t b_y = b.y;
    int64_t c_x = c.x;
    int64_t c_y = c.y;
    int64_t weight = c_x * (a_y - b_y) + c_y * (b_x - a_x) + a_x * b_y - b_x * a_y + bias;
    return (int32_t)(weight / SUBPIXEL_PRECISION);
}

} // namespace

// https://fgiesen.wordpress.com/2013/02/08/triangle-rasterization-in-practice/
// https://fgiesen.wordpress.com/2013/02/10/optimizing-the-basic-rasterizer/

template<typename AttributeType>
struct SampleAndComposeFunction
{
    virtual ~SampleAndComposeFunction() = default;

    virtual bool sample_and_compose(
        int pixel_x,
        int pixel_y,
        AttributeType attribute,
        hrz::MutImageView& output) = 0;
};

template<typename AttributeType>
void rasterize_triangle(
    hrz::MutImageView& output,
    lm::vec2 p0,
    lm::vec2 p1,
    lm::vec2 p2,
    AttributeType attr0,
    AttributeType attr1,
    AttributeType attr2,
    SampleAndComposeFunction<AttributeType>* sample_and_compose_function)
{
    int output_width = output.width;
    int output_height = output.height;
    lm::ivec2 output_size = {output_width, output_height};

    lm::vec2 p0_tile_f = p0 * output_size * SUBPIXEL_PRECISION;
    lm::vec2 p1_tile_f = p1 * output_size * SUBPIXEL_PRECISION;
    lm::vec2 p2_tile_f = p2 * output_size * SUBPIXEL_PRECISION;
    lm::ivec2 p0_tile = lm::ivec2(p0_tile_f);
    lm::ivec2 p1_tile = lm::ivec2(p1_tile_f);
    lm::ivec2 p2_tile = lm::ivec2(p2_tile_f);

    int xmin = std::min(std::min(p0_tile.x, p1_tile.x), p2_tile.x);
    int ymin = std::min(std::min(p0_tile.y, p1_tile.y), p2_tile.y);
    int xmax = std::max(std::max(p0_tile.x, p1_tile.x), p2_tile.x);
    int ymax = std::max(std::max(p0_tile.y, p1_tile.y), p2_tile.y);

    xmin = xmin - (xmin % SUBPIXEL_PRECISION);
    ymin = ymin - (ymin % SUBPIXEL_PRECISION);
    xmax = (xmax + SUBPIXEL_PRECISION) - (xmax % SUBPIXEL_PRECISION);
    ymax = (ymax + SUBPIXEL_PRECISION) - (ymax % SUBPIXEL_PRECISION);

    int x0 = std::min(std::max(xmin, 0), output_width * SUBPIXEL_PRECISION - 1);
    int y0 = std::min(std::max(ymin, 0), output_height * SUBPIXEL_PRECISION - 1);
    int x1 = std::min(std::max(xmax, 0), output_width * SUBPIXEL_PRECISION - 1);
    int y1 = std::min(std::max(ymax, 0), output_height * SUBPIXEL_PRECISION - 1);

    lm::ivec2 corner(x0 + SUBPIXEL_PRECISION / 2, y0 + SUBPIXEL_PRECISION / 2);

    auto step_01 = _weight_step(p0_tile, p1_tile);
    auto step_12 = _weight_step(p1_tile, p2_tile);
    auto step_20 = _weight_step(p2_tile, p0_tile);

    int bias0 = _is_top_left_edge(p1_tile, p2_tile) ? 0 : 1;
    int bias1 = _is_top_left_edge(p2_tile, p0_tile) ? 0 : 1;
    int bias2 = _is_top_left_edge(p0_tile, p1_tile) ? 0 : 1;

    int w2_row = _weight(p0_tile, p1_tile, corner, bias2);
    int w0_row = _weight(p1_tile, p2_tile, corner, bias0);
    int w1_row = _weight(p2_tile, p0_tile, corner, bias1);

    for (int y = y0; y <= y1; y += SUBPIXEL_PRECISION)
    {
        int w0 = w0_row;
        int w1 = w1_row;
        int w2 = w2_row;

        for (int x = x0; x <= x1; x += SUBPIXEL_PRECISION)
        {
            if ((w0 <= 0 && w1 <= 0 && w2 <= 0) && (w0 + w1 + w2) != 0)
            {
                AttributeType attr = (attr0 * w0 + attr1 * w1 + attr2 * w2) / (w0 + w1 + w2);

                int pixel_x = x / SUBPIXEL_PRECISION;
                int pixel_y = y / SUBPIXEL_PRECISION;

                sample_and_compose_function->sample_and_compose(pixel_x, pixel_y, attr, output);
            }

            w0 += step_12.x;
            w1 += step_20.x;
            w2 += step_01.x;
        }

        w0_row += step_12.y;
        w1_row += step_20.y;
        w2_row += step_01.y;
    }
}

} // namespace hrz_jobs::rasterizer
