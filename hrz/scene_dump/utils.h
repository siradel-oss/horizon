// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <span>
#include <stdint.h>
#include <vector>

namespace hrz::scene_dump
{

std::vector<std::byte> decompress(std::span<const std::byte> data);
uint32_t get_scene_dump_version(std::span<const std::byte> data);
std::vector<std::byte> set_scene_dump_version(std::span<const std::byte> data, uint32_t version);
std::vector<std::byte> read_file(const char* file_path);
bool write_file(const char* file, std::span<const std::byte> data);

} // namespace hrz::scene_dump
