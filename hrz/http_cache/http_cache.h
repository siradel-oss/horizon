// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/fnd/http.h"

namespace hrz
{

std::unique_ptr<IHttpLoader> create_http_cache_loader(
    IHttpClock*,
    std::unique_ptr<IHttpLoader> inner,
    size_t max_size);

} // namespace hrz
