#pragma once

namespace hrz
{
struct PlatformInfo
{
    enum Os
    {
        UnknownOs,
        Windows,
        Linux,
        MacOS,
        IOs,
        Android,
        ChromeOS,

        _OsCount,
    };

    static constexpr const char* OsName[_OsCount] = {
        "<unknown>", "Windows", "Linux", "MacOS", "iOS", "Android", "Chrome OS",
    };

    enum Runtime
    {
        UnknownRuntime,
        Native,
        Chrome,
        Firefox,
        Safari,
        Edge,

        _RuntimeCount,
    };

    static constexpr const char* RuntimeName[_RuntimeCount] = {
        "<unknown>", "Native", "Chrome", "Firefox", "Safari", "Edge",
    };

    // @Note We have no AMD GPU so it doesn't appear here for now.
    enum GpuVendor
    {
        UnknownGpuVendor,
        Nvidia,
        Intel,
        AppleSilicon,
        SwiftShader,
        Llvmpipe,

        _GpuVendorCount,
    };

    static constexpr const char* GpuVendorName[_GpuVendorCount] = {
        "<unknown>", "Nvidia", "Intel", "Apple Silicon", "SwiftShader", "LLVMpipe",
    };

    enum GpuFormFactor
    {
        UnknownFormFactor,
        Discrete,
        Integrated,
        Software,

        _GpuFormFactorCont,
    };

    static constexpr const char* GpuFormFactorName[_GpuFormFactorCont] = {
        "<unknown>",
        "Discrete",
        "Integrated",
        "Software emulated",
    };

    Os os;
    Runtime runtime;
    GpuVendor gpu_vendor;
    GpuFormFactor gpu_form_factor;

    bool is_mobile() const { return os == Os::Android || os == Os::IOs; }
};
} // namespace hrz
