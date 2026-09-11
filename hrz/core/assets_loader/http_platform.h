// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/fnd/http.h"

namespace hrz::assets_loader
{

// @Todo(HRZ-336): When no limit is set on the number of authorized parallel requests both on wasm
// and desktop we notice stalls in the rendering. The cause may be that too many data arrive in a
// single frame, and as every system tries to do as much work as possible it is slowing things down.
static const unsigned int MAX_AVAILABLE_HANDLES = 32;

#if HRZ_DESKTOP
std::unique_ptr<IHttpLoader> create_platform_loader(
    std::string_view user_agent,
    std::string_view http_referrer,
    size_t http_cache_size);
#else
std::unique_ptr<IHttpLoader> create_platform_loader();
#endif

} // namespace hrz::assets_loader
