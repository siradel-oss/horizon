#include "hrz_core_platform_detection.h"

#include <gtest/gtest.h>

namespace
{
using namespace hrz;

TEST(PlatformDetection, WindowsNativeNvidia)
{
    PlatformInfo info =
        detect_platform("Windows", "", "NVIDIA Corporation", "NVIDIA GeForce GTX 970/PCIe/SSE2");

    ASSERT_EQ(info.os, PlatformInfo::Windows);
    ASSERT_EQ(info.runtime, PlatformInfo::Native);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::Nvidia);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Discrete);
}

TEST(PlatformDetection, WindowsNativeNvidia2)
{
    PlatformInfo info =
        detect_platform("Windows", "", "NVIDIA Corporation", "GeForce GTX 1060 6GB/PCIe/SSE2");

    ASSERT_EQ(info.os, PlatformInfo::Windows);
    ASSERT_EQ(info.runtime, PlatformInfo::Native);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::Nvidia);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Discrete);
}

TEST(PlatformDetection, WindowsNativeNvidia3)
{
    PlatformInfo info =
        detect_platform("Windows", "", "NVIDIA Corporation", "Quadro P1000/PCIe/SSE2");

    ASSERT_EQ(info.os, PlatformInfo::Windows);
    ASSERT_EQ(info.runtime, PlatformInfo::Native);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::Nvidia);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Discrete);
}

TEST(PlatformDetection, WindowsNativeIntelHDGraphics)
{
    PlatformInfo info = detect_platform("Windows", "", "Intel", "Intel(R) UHD Graphics 630");

    ASSERT_EQ(info.os, PlatformInfo::Windows);
    ASSERT_EQ(info.runtime, PlatformInfo::Native);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::Intel);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, WindowsChromeNvidia)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/108.0.0.0 Safari/537.36",
        "Google Inc. (NVIDIA)",
        "ANGLE (NVIDIA, NVIDIA GeForce GTX 970 Direct3D11 vs_5_0 ps_5_0, D3D11)");

    ASSERT_EQ(info.os, PlatformInfo::Windows);
    ASSERT_EQ(info.runtime, PlatformInfo::Chrome);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::Nvidia);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Discrete);
}

TEST(PlatformDetection, WindowsChromeIntelHDGraphics)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/108.0.0.0 Safari/537.36",
        "Google Inc. (Intel)",
        "ANGLE (Intel, Intel(R) UHD Graphics 630 Direct3D11 vs_5_0 ps_5_0, D3D11)");

    ASSERT_EQ(info.os, PlatformInfo::Windows);
    ASSERT_EQ(info.runtime, PlatformInfo::Chrome);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::Intel);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, WindowsFirefoxNvidia)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:108.0) Gecko/20100101 Firefox/108.0",
        "Google Inc. (NVIDIA)", "ANGLE (NVIDIA, NVIDIA GeForce GTX 980 Direct3D11 vs_5_0 ps_5_0)");

    ASSERT_EQ(info.os, PlatformInfo::Windows);
    ASSERT_EQ(info.runtime, PlatformInfo::Firefox);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::Nvidia);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Discrete);
}

TEST(PlatformDetection, WindowsFirefoxIntelHDGraphics)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:108.0) Gecko/20100101 Firefox/108.0",
        "Google Inc. (Intel)", "ANGLE (Intel, Intel(R) HD Graphics 400 Direct3D11 vs_5_0 ps_5_0)");

    ASSERT_EQ(info.os, PlatformInfo::Windows);
    ASSERT_EQ(info.runtime, PlatformInfo::Firefox);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::Intel);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, LinuxChromeIntelHDGraphics)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/108.0.0.0 "
        "Safari/537.36",
        "Google Inc. (Intel)", "ANGLE (Intel, Mesa Intel(R) UHD Graphics (TGL GT1), OpenGL 4.6)");

    ASSERT_EQ(info.os, PlatformInfo::Linux);
    ASSERT_EQ(info.runtime, PlatformInfo::Chrome);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::Intel);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, LinuxChromeSwiftShader)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/108.0.0.0 "
        "Safari/537.36",
        "Google Inc. (Google)",
        "ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device (Subzero) (0x0000C0DE)), SwiftShader "
        "driver)");

    ASSERT_EQ(info.os, PlatformInfo::Linux);
    ASSERT_EQ(info.runtime, PlatformInfo::Chrome);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::SwiftShader);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Software);
}

TEST(PlatformDetection, LinuxFirefoxIntelHDGraphics)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:108.0) Gecko/20100101 Firefox/108.0", "Intel",
        "Intel(R) HD Graphics");

    ASSERT_EQ(info.os, PlatformInfo::Linux);
    ASSERT_EQ(info.runtime, PlatformInfo::Firefox);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::Intel);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, LinuxNativeIntelHDGraphics)
{
    PlatformInfo info =
        detect_platform("Linux", "", "Intel", "Mesa Intel(R) UHD Graphics (TGL GT1)");

    ASSERT_EQ(info.os, PlatformInfo::Linux);
    ASSERT_EQ(info.runtime, PlatformInfo::Native);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::Intel);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, LinuxNativeNvidia)
{
    PlatformInfo info =
        detect_platform("Linux", "", "NVIDIA Corporation", "NVIDIA RTX A2000 Laptop GPU/PCIe/SSE2");

    ASSERT_EQ(info.os, PlatformInfo::Linux);
    ASSERT_EQ(info.runtime, PlatformInfo::Native);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::Nvidia);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Discrete);
}

TEST(PlatformDetection, LinuxNativeLlvmpipe)
{
    PlatformInfo info = detect_platform("Linux", "", "Mesa", "llvmpipe (LLVM 20.1.2, 256 bits)");

    ASSERT_EQ(info.os, PlatformInfo::Linux);
    ASSERT_EQ(info.runtime, PlatformInfo::Native);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::Llvmpipe);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Software);
}

TEST(PlatformDetection, AndroidChrome)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (Linux; Android 12; SM-T870) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/106.0.0.0 Safari/537.36",
        "Google Inc. (Qualcomm)", "ANGLE (Qualcomm, Adreno (TM) 650, OpenGL ES 3.2)");

    ASSERT_EQ(info.os, PlatformInfo::Android);
    ASSERT_EQ(info.runtime, PlatformInfo::Chrome);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::UnknownGpuVendor);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, AndroidFirefox)
{
    PlatformInfo info = detect_platform(
        "Emscripten", "Mozilla/5.0 (Android 12; Mobile; rv:105.0) Gecko/105.0 Firefox/105.0",
        "Qualcomm", "Adreno (TM) 650");

    ASSERT_EQ(info.os, PlatformInfo::Android);
    ASSERT_EQ(info.runtime, PlatformInfo::Firefox);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::UnknownGpuVendor);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, MacChrome)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/108.0.0.0 Safari/537.36",
        "Google Inc. (Apple)", "ANGLE (Apple, Apple M1, OpenGL 4.1)");

    ASSERT_EQ(info.os, PlatformInfo::MacOS);
    ASSERT_EQ(info.runtime, PlatformInfo::Chrome);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::AppleSilicon);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, MacSafari)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) "
        "Version/16.1 Safari/605.1.15",
        "Apple Inc.", "Apple GPU");

    ASSERT_EQ(info.os, PlatformInfo::MacOS);
    ASSERT_EQ(info.runtime, PlatformInfo::Safari);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::AppleSilicon);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, IPhoneChrome)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (iPhone; CPU iPhone OS 16_2 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like "
        "Gecko) CriOS/108.0.5359.112 Mobile/15E148 Safari/604.1",
        "", "");

    ASSERT_EQ(info.os, PlatformInfo::IOs);
    ASSERT_EQ(info.runtime, PlatformInfo::Chrome);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::UnknownGpuVendor);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, IPadChrome)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (iPad; CPU OS 16_2 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) "
        "CriOS/108.0.5359.112 Mobile/15E148 Safari/604.1",
        "", "");

    ASSERT_EQ(info.os, PlatformInfo::IOs);
    ASSERT_EQ(info.runtime, PlatformInfo::Chrome);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::UnknownGpuVendor);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, IPhoneFirefox)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (iPhone; CPU iPhone OS 13_1 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like "
        "Gecko) FxiOS/108.0 Mobile/15E148 Safari/605.1.15",
        "", "");

    ASSERT_EQ(info.os, PlatformInfo::IOs);
    ASSERT_EQ(info.runtime, PlatformInfo::Firefox);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::UnknownGpuVendor);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, IPadFirefox)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (iPad; CPU OS 13_1 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) "
        "FxiOS/108.0 Mobile/15E148 Safari/605.1.15",
        "", "");

    ASSERT_EQ(info.os, PlatformInfo::IOs);
    ASSERT_EQ(info.runtime, PlatformInfo::Firefox);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::UnknownGpuVendor);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, IPhoneSafari)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (iPhone; CPU iPhone OS 16_2 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like "
        "Gecko) Version/16.1 Mobile/15E148 Safari/604.1",
        "", "");

    ASSERT_EQ(info.os, PlatformInfo::IOs);
    ASSERT_EQ(info.runtime, PlatformInfo::Safari);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::UnknownGpuVendor);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, IPadSafari)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (iPad; CPU OS 16_2 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) "
        "Version/16.1 Mobile/15E148 Safari/604.1",
        "", "");

    ASSERT_EQ(info.os, PlatformInfo::IOs);
    ASSERT_EQ(info.runtime, PlatformInfo::Safari);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::UnknownGpuVendor);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, WindowsEdge)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/108.0.0.0 Safari/537.36 Edg/108.0.1462.54",
        "", "");

    ASSERT_EQ(info.os, PlatformInfo::Windows);
    ASSERT_EQ(info.runtime, PlatformInfo::Edge);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::UnknownGpuVendor);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Discrete);
}

TEST(PlatformDetection, MacOSEdge)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 13_1) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/108.0.0.0 Safari/537.36 Edg/108.0.1462.54",
        "", "");

    ASSERT_EQ(info.os, PlatformInfo::MacOS);
    ASSERT_EQ(info.runtime, PlatformInfo::Edge);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::UnknownGpuVendor);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Discrete);
}

TEST(PlatformDetection, IOsEdge)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (iPhone; CPU iPhone OS 16_2 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like "
        "Gecko) Version/16.0 EdgiOS/108.1462.54 Mobile/15E148 Safari/605.1.15",
        "", "");

    ASSERT_EQ(info.os, PlatformInfo::IOs);
    ASSERT_EQ(info.runtime, PlatformInfo::Edge);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::UnknownGpuVendor);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, AndroidEdge)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (Linux; Android 10; HD1913) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/108.0.5359.128 Mobile Safari/537.36 EdgA/108.0.1462.48",
        "", "");

    ASSERT_EQ(info.os, PlatformInfo::Android);
    ASSERT_EQ(info.runtime, PlatformInfo::Edge);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::UnknownGpuVendor);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

TEST(PlatformDetection, ChromeOSChrome)
{
    PlatformInfo info = detect_platform(
        "Emscripten",
        "Mozilla/5.0 (X11; CrOS x86_64 15183.59.0) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/108.0.5359.75 Safari/537.36",
        "", "");

    ASSERT_EQ(info.os, PlatformInfo::ChromeOS);
    ASSERT_EQ(info.runtime, PlatformInfo::Chrome);
    ASSERT_EQ(info.gpu_vendor, PlatformInfo::UnknownGpuVendor);
    ASSERT_EQ(info.gpu_form_factor, PlatformInfo::GpuFormFactor::Integrated);
}

} // namespace
