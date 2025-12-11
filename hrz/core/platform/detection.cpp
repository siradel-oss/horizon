#include "hrz/core/platform/detection.h"

#include "hrz/fnd/defines.h"

#include <assert.h>

#ifdef HRZ_EMSCRIPTEN
#    include "hrz/core/js/lib.h"
#endif

namespace hrz
{
constexpr const char* PlatformInfo::OsName[PlatformInfo::_OsCount];
constexpr const char* PlatformInfo::RuntimeName[PlatformInfo::_RuntimeCount];
constexpr const char* PlatformInfo::GpuVendorName[PlatformInfo::_GpuVendorCount];
constexpr const char* PlatformInfo::GpuFormFactorName[PlatformInfo::_GpuFormFactorCont];

PlatformInfo detect_platform(
    std::string_view platform,
    std::string_view user_agent,
    std::string_view gl_vendor,
    std::string_view gl_renderer)
{
    static constexpr auto npos = std::string_view::npos;

    PlatformInfo info = {};

    if (platform == "Windows")
    {
        info.os = PlatformInfo::Windows;
        info.runtime = PlatformInfo::Native;
    }
    else if (platform == "Linux")
    {
        info.os = PlatformInfo::Linux;
        info.runtime = PlatformInfo::Native;
    }
    else if (platform == "Emscripten")
    {
        if (user_agent.find("Windows NT") != npos)
        {
            info.os = PlatformInfo::Windows;
        }
        else if (user_agent.find("Android") != npos)
        {
            info.os = PlatformInfo::Android;
        }
        // Don't place this before "Android", because some Android devices have
        // "Linux" in their user agent.
        else if (user_agent.find("Linux") != npos)
        {
            info.os = PlatformInfo::Linux;
        }
        else if (user_agent.find("Macintosh") != npos)
        {
            info.os = PlatformInfo::MacOS;
        }
        else if (user_agent.find("iPhone") != npos || user_agent.find("iPad") != npos)
        {
            info.os = PlatformInfo::IOs;
        }
        else if (user_agent.find("CrOS") != npos)
        {
            info.os = PlatformInfo::ChromeOS;
        }

        if (user_agent.find("Edg/") != npos)
        {
            // Don't place this after "Chrome" because edge has Chrome in its
            // user-agent.
            info.runtime = PlatformInfo::Edge;
        }
        else if (user_agent.find("EdgiOS/") != npos && info.os == PlatformInfo::IOs)
        {
            info.runtime = PlatformInfo::Edge;
        }
        else if (user_agent.find("EdgA/") != npos && info.os == PlatformInfo::Android)
        {
            info.runtime = PlatformInfo::Edge;
        }
        else if (user_agent.find("Chrome/") != npos)
        {
            info.runtime = PlatformInfo::Chrome;
        }
        else if (user_agent.find("CriOS/") != npos && info.os == PlatformInfo::IOs)
        {
            info.runtime = PlatformInfo::Chrome;
        }
        else if (user_agent.find("Firefox/") != npos)
        {
            info.runtime = PlatformInfo::Firefox;
        }
        else if (user_agent.find("FxiOS/") != npos && info.os == PlatformInfo::IOs)
        {
            info.runtime = PlatformInfo::Firefox;
        }
        else if (user_agent.find("Safari/") != npos)
        {
            // Don't place this before "Chrome" or "Edge" because Chromium has "Safari" in
            // its user agent.
            info.runtime = PlatformInfo::Safari;
        }
    }
    else
    {
        assert(!"Unhandled platform, or maybe wrong input platform?");
    }

    if (info.os == PlatformInfo::Android || info.os == PlatformInfo::ChromeOS
        || info.os == PlatformInfo::IOs)
    {
        info.gpu_form_factor = PlatformInfo::GpuFormFactor::Integrated;
    }
    else
    {
        info.gpu_form_factor = PlatformInfo::GpuFormFactor::UnknownFormFactor;
    }

    if (gl_vendor.find("NVIDIA") != npos || gl_renderer.find("NVIDIA") != npos
        || gl_renderer.find("Quadro") != npos)
    {
        info.gpu_vendor = PlatformInfo::Nvidia;
        info.gpu_form_factor = PlatformInfo::GpuFormFactor::Discrete;
    }
    else if (gl_vendor.find("Intel") != npos || gl_renderer.find("Intel") != npos)
    {
        info.gpu_vendor = PlatformInfo::Intel;
        if (gl_renderer.find("HD Graphics") != npos)
        {
            info.gpu_form_factor = PlatformInfo::GpuFormFactor::Integrated;
        }
        else if (gl_renderer.find("Arc") != npos)
        {
            info.gpu_form_factor = PlatformInfo::GpuFormFactor::Discrete;
        }
    }
    else if (gl_renderer.find("Apple") != npos)
    {
        info.gpu_vendor = PlatformInfo::AppleSilicon;
        info.gpu_form_factor = PlatformInfo::GpuFormFactor::Integrated;
    }
    else if (gl_renderer.find("SwiftShader") != npos)
    {
        info.gpu_vendor = PlatformInfo::SwiftShader;
        info.gpu_form_factor = PlatformInfo::GpuFormFactor::Software;
    }
    else if (gl_renderer.find("llvmpipe") != npos)
    {
        info.gpu_vendor = PlatformInfo::Llvmpipe;
        info.gpu_form_factor = PlatformInfo::GpuFormFactor::Software;
    }
    else if (gl_vendor.find("AMD") != npos || gl_renderer.find("AMD") != npos)
    {
        info.gpu_vendor = PlatformInfo::Amd;
        if (gl_renderer.find("Ryzen") != npos || gl_renderer.find("Graphics") != npos)
        {
            // Not super precise
            info.gpu_form_factor = PlatformInfo::GpuFormFactor::Integrated;
        }
        else
        {
            info.gpu_form_factor = PlatformInfo::GpuFormFactor::Discrete;
        }
    }

    return info;
}

} // namespace hrz
