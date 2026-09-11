// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_allocator.h"

namespace hrz_jobs
{

struct CesiumTerrainTileData
{
    hrz::blobs::BlobHandle blob;
    std::string format;
};

} // namespace hrz_jobs
