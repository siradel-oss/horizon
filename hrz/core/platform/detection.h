#pragma once

#include "hrz/common/platform_detection.h"

#include <string_view>

namespace hrz
{
// platform is the platform name (Windows, Linux, Emscripten).
// user_agent is non-empty only in web contexts.
// gl_vendor and gl_renderer and given by Mycelium.
PlatformInfo detect_platform(
    std::string_view platform,
    std::string_view user_agent,
    std::string_view gl_vendor,
    std::string_view gl_renderer);

} // namespace hrz
