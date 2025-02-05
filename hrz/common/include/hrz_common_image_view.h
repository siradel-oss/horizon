#pragma once

#include <hrz_protocol_all.h>

#include <gsl/gsl-lite.hpp>

namespace hrz
{
// This struct does not own the pixel data.
template<typename Backing>
struct ImageViewBase
{
    gsl::span<Backing> data;
    hrz_proto::ImageFormat format;
    uint32_t width;
    uint32_t height;

    ImageViewBase(
        gsl::span<Backing> data,
        hrz_proto::ImageFormat format,
        uint32_t width,
        uint32_t height) :
        data(data), format(format), width(width), height(height)
    {
    }

    template<typename T, unsigned int CHANNELS>
    inline const T* pixel_data(int x, int y) const
    {
        assert(x >= 0 && x < width && y >= 0 && y < height);
        return (const T*)(data.data()) + ((x + y * width) * CHANNELS);
    }

    template<typename T, unsigned int CHANNELS>
    inline T* pixel_data(int x, int y)
    {
        assert(x >= 0 && x < width && y >= 0 && y < height);
        return (T*)(data.data()) + ((x + y * width) * CHANNELS);
    }
};

struct ImageView : public ImageViewBase<const std::byte>
{
    ImageView(
        gsl::span<const std::byte> data,
        hrz_proto::ImageFormat format,
        uint32_t width,
        uint32_t height) :
        ImageViewBase(data, format, width, height)
    {
    }

    template<typename T, unsigned int CHANNELS>
    inline const T* pixel_data(int x, int y) const
    {
        assert(x >= 0 && x < width && y >= 0 && y < height);
        return (const T*)(data.data()) + ((x + y * width) * CHANNELS);
    }
};

struct MutImageView : public ImageViewBase<std::byte>
{
    MutImageView(
        gsl::span<std::byte> data,
        hrz_proto::ImageFormat format,
        uint32_t width,
        uint32_t height) :
        ImageViewBase(data, format, width, height)
    {
    }

    template<typename T, unsigned int CHANNELS>
    inline T* pixel_data(int x, int y)
    {
        assert(x >= 0 && x < width && y >= 0 && y < height);
        return (T*)(data.data()) + ((x + y * width) * CHANNELS);
    }
};
} // namespace hrz
