#include "hrz/common/compression.h"

#include "rules_cc/cc/runfiles/runfiles.h"

#include <gtest/gtest.h>

#include <fstream>
#include <optional>

using rules_cc::cc::runfiles::Runfiles;

class Compression : public testing::Test
{
protected:
    Runfiles* _runfiles;

public:
    Compression() : testing::Test()
    {
        std::string error;
        _runfiles = Runfiles::Create(::testing::internal::GetArgvs()[0], &error);
    }
};

std::optional<std::vector<std::byte>> read_file(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        return std::nullopt;
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<std::byte> content(size);
    file.read((char*)content.data(), size);

    return content;
}

TEST_F(Compression, DecompressGzip)
{
    auto reference =
        read_file(_runfiles->Rlocation("horizon/hrz/common/tests_data/lorem_ipsum.txt").c_str());
    auto compressed =
        read_file(_runfiles->Rlocation("horizon/hrz/common/tests_data/lorem_ipsum.txt.gz").c_str());

    std::vector<std::byte> decompressed;
    auto callback = [&](std::span<const std::byte> chunk)
    { decompressed.insert(decompressed.end(), chunk.begin(), chunk.end()); };

    ASSERT_TRUE(reference.has_value());
    ASSERT_TRUE(compressed.has_value());
    ASSERT_TRUE(hrz::decompress_gzip(compressed.value(), callback));

    ASSERT_EQ(reference.value().size(), decompressed.size());
    ASSERT_EQ(reference.value(), decompressed);
}

TEST_F(Compression, DecompressBrotli)
{
    auto reference =
        read_file(_runfiles->Rlocation("horizon/hrz/common/tests_data/lorem_ipsum.txt").c_str());
    auto compressed =
        read_file(_runfiles->Rlocation("horizon/hrz/common/tests_data/lorem_ipsum.txt.br").c_str());

    std::vector<std::byte> decompressed;
    auto callback = [&](std::span<const std::byte> chunk)
    { decompressed.insert(decompressed.end(), chunk.begin(), chunk.end()); };

    ASSERT_TRUE(reference.has_value());
    ASSERT_TRUE(compressed.has_value());
    ASSERT_TRUE(hrz::decompress_brotli(compressed.value(), callback));

    ASSERT_EQ(reference.value().size(), decompressed.size());
    ASSERT_EQ(reference.value(), decompressed);
}

TEST_F(Compression, DecompressZstd)
{
    auto reference =
        read_file(_runfiles->Rlocation("horizon/hrz/common/tests_data/lorem_ipsum.txt").c_str());
    auto compressed = read_file(
        _runfiles->Rlocation("horizon/hrz/common/tests_data/lorem_ipsum.txt.zst").c_str());

    std::vector<std::byte> decompressed;
    auto callback = [&](std::span<const std::byte> chunk)
    { decompressed.insert(decompressed.end(), chunk.begin(), chunk.end()); };

    ASSERT_TRUE(reference.has_value());
    ASSERT_TRUE(compressed.has_value());
    ASSERT_TRUE(hrz::decompress_zstd(compressed.value(), callback));

    ASSERT_EQ(reference.value().size(), decompressed.size());
    ASSERT_EQ(reference.value(), decompressed);
}
