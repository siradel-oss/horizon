// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <stddef.h>

namespace hrz::shaders_decompressor
{

std::pair<std::unique_ptr<char[]>, size_t> decompress(const char* buffer, size_t size);

} // namespace hrz::shaders_decompressor
