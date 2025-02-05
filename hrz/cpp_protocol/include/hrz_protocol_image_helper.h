#pragma once

#include "hrz_protocol_all.h"

namespace hrz_proto
{
unsigned int channel_count(ImageFormat format);
unsigned int bit_count(ImageFormat format);
unsigned int byte_count(ImageFormat format);
} // namespace hrz_proto
