#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <math.h>
#include <stb_image.h>
#include <stb_image_write.h>

#include <vector>

struct RGBA8
{
    uint8_t r, g, b, a;
};

struct RGBf
{
    float r, g, b;
};

struct LAB
{
    float l, a, b;
};

float clampf(float f, float a = 0.0f, float b = 1.0f)
{
    return std::min(std::max(f, a), b);
}

RGBf srgb_to_linear(RGBA8 from)
{
    return RGBf{
        ::powf((float)from.r / 255.0f, 2.2f),
        ::powf((float)from.g / 255.0f, 2.2f),
        ::powf((float)from.b / 255.0f, 2.2f),
    };
}

RGBA8 linear_to_srgb(RGBf from)
{
    return RGBA8{
        (uint8_t)(clampf(::powf(from.r, 0.45f)) * 255.0f),
        (uint8_t)(clampf(::powf(from.g, 0.45f)) * 255.0f),
        (uint8_t)(clampf(::powf(from.b, 0.45f)) * 255.0f),
        255,
    };
}

LAB linear_srgb_to_oklab(RGBf c)
{
    float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;

    float l_ = ::cbrtf(l);
    float m_ = ::cbrtf(m);
    float s_ = ::cbrtf(s);

    return {
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_,
    };
}

LAB srgb_to_oklab(RGBA8 rgb)
{
    return linear_srgb_to_oklab(srgb_to_linear(rgb));
}

float diff_oklab(LAB a, LAB b)
{
    LAB d = {
        a.l - b.l,
        a.a - b.a,
        a.b - b.b,
    };
    return ::sqrtf(d.l * d.l + d.a * d.a + d.b * d.b);
}

float diff_srgb(RGBA8 a, RGBA8 b)
{
    RGBf d = {
        (float)a.r / 255.0f - (float)b.r / 255.0f,
        (float)a.g / 255.0f - (float)b.g / 255.0f,
        (float)a.b / 255.0f - (float)b.b / 255.0f,
    };
    return ::sqrtf(d.r * d.r + d.g * d.g + d.b * d.b);
}

RGBA8 scalar_to_srgb(float a)
{
    return RGBA8{
        (uint8_t)(clampf(a) * 255.0f),
        (uint8_t)(clampf(a) * 255.0f),
        (uint8_t)(clampf(a) * 255.0f),
        255,
    };
}

template<typename Pixel>
struct Image
{
    int width;
    int height;
    std::vector<Pixel> data;

    Image(const Image&) = default;
    Image(Image&&) = default;

    Image(int w, int h) : width(w), height(h), data(w * h) {}

    Pixel* pixel(int x, int y) { return data.data() + x + y * width; }

    const Pixel* pixel(int x, int y) const { return data.data() + x + y * width; }

    void copy_border_one()
    {
        Pixel* pixels = data.data();

        // Extend left and right columns outwards
        for (int y = 1; y < height - 1; ++y)
        {
            pixels[y * width + 0] = pixels[y * width + 1];
            pixels[y * width + width - 1] = pixels[y * width + width - 2];
        }

        // Extend top and bottom rows outwards
        memcpy(pixels, pixels + width, sizeof(Pixel) * width);
        memcpy(pixels + width * (height - 1), pixels + width * (height - 2), sizeof(Pixel) * width);
    }

    void extend_one()
    {
        int dst_width = width + 2;
        int dst_height = height + 2;

        std::vector<Pixel> new_data(dst_width * dst_height);

        const Pixel* src = data.data();
        Pixel* dst = new_data.data();

        // Copy the middle part
        for (int y = 0; y < height; ++y)
        {
            memcpy(dst + dst_width * (y + 1) + 1, src + y * width, sizeof(Pixel) * width);
        }

        width = dst_width;
        height = dst_height;
        data.swap(new_data);

        copy_border_one();
    }

    void contract_one()
    {
        int dst_width = width - 2;
        int dst_height = height - 2;

        std::vector<Pixel> new_data(dst_width * dst_height);

        const Pixel* src = data.data();
        Pixel* dst = new_data.data();

        // Copy the middle part
        for (int y = 0; y < dst_height; ++y)
        {
            memcpy(dst + dst_width * y, src + (y + 1) * width + 1, sizeof(Pixel) * dst_width);
        }

        width = dst_width;
        height = dst_height;
        data.swap(new_data);
    }
};

template<typename FromPixel, typename ToPixel, typename... Args>
Image<ToPixel> op(const Image<FromPixel>& src, ToPixel (*xform)(FromPixel, Args...), Args&&... args)
{
    Image<ToPixel> dst(src.width, src.height);

    int count = src.width * src.height;
    for (int i = 0; i < count; ++i)
    {
        dst.data[i] = xform(src.data[i], std::forward<Args>(args)...);
    }

    return dst;
}

template<typename XForm, typename Pixel, typename... Args>
void op_in_place(Image<Pixel>& img, XForm xform, Args&&... args)
{
    int count = img.width * img.height;
    for (int i = 0; i < count; ++i)
    {
        img.data[i] = xform(img.data[i], std::forward<Args>(args)...);
    }
}

template<typename FromPixel, typename ToPixel, typename... Args>
Image<ToPixel> bin_op(
    const Image<FromPixel>& a,
    const Image<FromPixel>& b,
    ToPixel (*xform)(FromPixel, FromPixel, Args...),
    Args&&... args)
{
    assert(a.width == b.width && a.height == b.height);

    Image<ToPixel> to(a.width, a.height);

    int count = a.width * a.height;
    for (int i = 0; i < count; ++i)
    {
        to.data[i] = xform(a.data[i], b.data[i], std::forward<Args>(args)...);
    }

    return to;
}

// dst = min(a, b, c)
void erode(const float* a, const float* b, const float* c, float* dst, int count)
{
    for (int i = 0; i < count; ++i)
    {
        *dst++ = std::min(std::min(*a++, *b++), *c++);
    }
}

void erode_one(Image<float>& img)
{
    img.extend_one();

    Image<float> tmp(img.width, img.height);

    // We do a vertical pass, then an horizontal one, so we have a but less compares to do.
    // We use the temporary image to store the intermediate result.
    for (int y = 1; y < img.height - 1; ++y)
    {
        erode(
            img.pixel(1, y - 1), img.pixel(1, y), img.pixel(1, y + 1), tmp.pixel(1, y),
            img.width - 2);
    }

    tmp.copy_border_one();

    for (int y = 1; y < img.height - 1; ++y)
    {
        erode(tmp.pixel(0, y), img.pixel(1, y), img.pixel(2, y), img.pixel(1, y), img.width - 2);
    }

    img.contract_one();
}

float threshold(float in, float t)
{
    if (in >= t)
        return 1.0f;
    else
        return 0.0f;
}

float count_ones(float in, int* count)
{
    if (in == 1.0f) *count += 1;
    return in;
}

Image<RGBA8> load_image(const char* filename)
{
    int width, height, channels;
    void* data = stbi_load(filename, &width, &height, &channels, 4);
    Image<RGBA8> img(width, height);
    memcpy(img.data.data(), data, 4 * width * height);
    return img;
}

void save_image(const char* filename, const Image<RGBA8>& img)
{
    stbi_write_png(filename, img.width, img.height, 4, img.data.data(), 4 * img.width);
}

int main(int argc, char* argv[])
{
    if (argc < 4)
    {
        printf(
            "Usage: %s INPUT_REFERENCE_CAPTURE_PNG INPUT_CAPTURE_PNG OUTPUT_DIFF_PNG [additional "
            "options]\n",
            argv[0]);
        printf("The number of different pixels will be written to the standard output stream.\n");
        printf("Additional options:\n");
        printf(
            "  --[no-]erode           Enable or disable filtering out very small differences. "
            "Default: enabled.\n");
        printf(
            "  --threshold <value>    Set the threshold value above which a difference is "
            "considered an error. Default: 0.05.\n");
        return 1;
    }

    bool erode = true;
    float threshold_value = 0.05f;

    const char* expected_path = argv[1];
    const char* captured_path = argv[2];
    const char* diff_path = argv[3];

    for (int i = 4; i < argc; ++i)
    {
        bool is_last = i >= argc - 1;
        if (strcmp(argv[i], "--no-erode") == 0)
        {
            erode = false;
        }
        else if (strcmp(argv[i], "--erode") == 0)
        {
            erode = true;
        }
        else if (!is_last && strcmp(argv[i], "--threshold") == 0)
        {
            threshold_value = atof(argv[i + 1]);
            i += 1;
        }
    }

    Image<RGBA8> expected_rgb = load_image(expected_path);
    Image<RGBA8> captured_rgb = load_image(captured_path);

    if (expected_rgb.width != captured_rgb.width || expected_rgb.height != captured_rgb.height)
    {
        printf("Image size mismatch.\n");
        return 1;
    }

    auto expected_lab = op(expected_rgb, srgb_to_oklab);
    auto captured_lab = op(captured_rgb, srgb_to_oklab);

    auto diff = bin_op(expected_lab, captured_lab, diff_oklab);

    if (erode)
    {
        erode_one(diff);
    }

    op_in_place(diff, threshold, threshold_value);

    int error_count = 0;
    op_in_place(diff, count_ones, &error_count);

    save_image(diff_path, op(diff, scalar_to_srgb));

    printf("%d\n", error_count);

    return 0;
}
