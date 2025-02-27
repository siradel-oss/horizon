#pragma once

#if defined(_WIN32)
#    define HRZ_WINDOWS 1
#    define HRZ_PLATFORM_NAME "Windows"
#elif defined(__linux__)
#    define HRZ_LINUX 1
#    define HRZ_PLATFORM_NAME "Linux"
#elif defined(__EMSCRIPTEN__)
#    define HRZ_EMSCRIPTEN 1
#    define HRZ_PLATFORM_NAME "Emscripten"
#endif

#if HRZ_WINDOWS || HRZ_LINUX
#    define HRZ_DESKTOP 1
#    define HRZ_INTERNAL_INTEGRATION 1
#endif

#if HRZ_WINDOWS
#    ifdef HRZ_API_EXPORT
#        define HRZ_API __declspec(dllexport)
#    else
#        define HRZ_API __declspec(dllimport)
#    endif
#elif HRZ_LINUX
#    define HRZ_API __attribute__((visibility("default")))
#else
#    define HRZ_API
#endif

#if _DEBUG || !defined(NDEBUG)
#    define HRZ_DEBUG 1
#endif

#define HRZ_CONCAT2(A, B) A##B
#define HRZ_CONCAT(A, B) HRZ_CONCAT2(A, B)
