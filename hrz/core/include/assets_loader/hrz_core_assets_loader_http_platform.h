#pragma once

#include "assets_loader/hrz_core_assets_loader.h"

#include <hrz_fnd_http.h>

namespace hrz::assets_loader
{
// @Todo(HRZ-336): When no limit is set on the number of authorized parallel requests both on wasm
// and desktop we notice stalls in the rendering. The cause may be that too many data arrive in a
// single frame, and as every system tries to do as much work as possible it is slowing things down.
static const unsigned int MAX_AVAILABLE_HANDLES = 32;

#if HRZ_DESKTOP
std::unique_ptr<IHttpLoader> create_platform_loader(
    const char* user_agent,
    const char* http_referrer,
    size_t http_cache_size);
#else
std::unique_ptr<IHttpLoader> create_platform_loader();
#endif

} // namespace hrz::assets_loader
