#pragma once

#include <hrz_fnd_http.h>

namespace hrz
{
std::unique_ptr<IHttpLoader> create_http_cache_loader(
    IHttpClock*,
    std::unique_ptr<IHttpLoader> inner,
    size_t max_size);

} // namespace hrz
