#include "hrz_shaders_decompressor.h"

#include <lz4.h>
#include <string.h>

#include <vector>

namespace hrz::shaders_decompressor
{
std::pair<std::unique_ptr<char[]>, size_t> decompress(const char* input, size_t input_size)
{
    const size_t limit_size = input_size * 10'000;

    std::vector<char> output_buffer;
    size_t capacity = input_size;

    while (capacity < limit_size)
    {
        capacity *= 2;

        output_buffer.clear(); // Avoids a copy when reallocating
        output_buffer.resize(capacity);

        int result =
            LZ4_decompress_safe(input, output_buffer.data(), (int)input_size, (int)capacity);
        if (result >= 0)
        {
            std::unique_ptr<char[]> output(new char[result]);
            memcpy(output.get(), output_buffer.data(), result);
            return {std::move(output), (size_t)result};
        }
    }
    return {nullptr, 0};
}

} // namespace hrz::shaders_decompressor
