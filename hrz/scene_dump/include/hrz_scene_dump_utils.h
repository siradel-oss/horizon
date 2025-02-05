#pragma once

#include <gsl/gsl-lite.hpp>

#include <stdint.h>
#include <vector>

namespace hrz::scene_dump
{

std::vector<std::byte> decompress(gsl::span<const std::byte> data);
uint32_t get_scene_dump_version(gsl::span<const std::byte> data);
std::vector<std::byte> set_scene_dump_version(gsl::span<const std::byte> data, uint32_t version);
std::vector<std::byte> read_file(const char* file_path);
bool write_file(const char* file, gsl::span<const std::byte> data);

} // namespace hrz::scene_dump
