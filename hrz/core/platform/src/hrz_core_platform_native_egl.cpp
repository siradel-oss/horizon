#include <hrz_fnd_defines.h>

#if HRZ_LINUX
#    include "hrz_core_platform_linux_x11.h"
#elif HRZ_WINDOWS
#    include "hrz_core_platform_windows.h"

#    include <windows.h>
#endif

#include <hrz_fnd_log.h>

#include <glad/egl.h>

// The following extensions are unfortunately not available in Glad:

// From the ANGLE_platform_angle extension
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
#define EGL_PLATFORM_ANGLE_MAX_VERSION_MAJOR_ANGLE 0x3204
#define EGL_PLATFORM_ANGLE_MAX_VERSION_MINOR_ANGLE 0x3205
#define EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED 0x3451
#define EGL_PLATFORM_ANGLE_NATIVE_PLATFORM_TYPE_ANGLE 0x348F
#define EGL_PLATFORM_ANGLE_TYPE_DEFAULT_ANGLE 0x3206
#define EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE 0x320A
#define EGL_PLATFORM_ANGLE_DEVICE_TYPE_NULL_ANGLE 0x345E
#define EGL_PLATFORM_X11_EXT 0x31D5
#define EGL_PLATFORM_DEVICE_EXT 0x313F
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD

// From the ANGLE_platform_angle_opengl extension
#define EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE 0x320D
#define EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE 0x320E
#define EGL_PLATFORM_ANGLE_EGL_HANDLE_ANGLE 0x3480

// From the ANGLE_platform_angle_vulkan extension
#define EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE 0x3450

namespace hrz::platform
{
struct GlContext
{
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLSurface egl_surface;
    EGLContext egl_ctx;
};

GlLoadFn get_gl_load_fn()
{
    return (GlLoadFn)eglGetProcAddress;
}

hrz_proto::ViewerInitStatus initialize_gl_ctx(PlatformContext* ctx)
{
    GlContext gl_ctx;

#if HRZ_LINUX
    EGLNativeDisplayType native_display = (EGLNativeDisplayType)ctx->display;
    EGLNativeWindowType native_window = (EGLNativeWindowType)ctx->window;
#elif HRZ_WINDOWS
    EGLNativeDisplayType native_display = (EGLNativeDisplayType)GetDC(ctx->hwnd);
    EGLNativeWindowType native_window = (EGLNativeWindowType)ctx->hwnd;
#endif

    if (!gladLoaderLoadEGL(nullptr))
    {
        HRZ_LOG_ERROR("Couldn't load EGL");
        return hrz_proto::ViewerInitStatus::FRAMEBUFFER_CREATION_ERROR;
    }

    const char* extension_string =
        static_cast<const char*>(eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));
    if (!extension_string)
    {
        // Fallback to an empty string for strstr()
        extension_string = "";
    }

    if (strstr(extension_string, "EGL_ANGLE_platform_angle"))
    {
        // GLES provider is Angle

        if (eglGetPlatformDisplay)
        {
            std::vector<EGLAttrib> display_attributes;
            display_attributes.push_back(EGL_PLATFORM_ANGLE_TYPE_ANGLE);
            display_attributes.push_back(EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE);
            display_attributes.push_back(EGL_PLATFORM_ANGLE_MAX_VERSION_MAJOR_ANGLE);
            display_attributes.push_back(4);
            display_attributes.push_back(EGL_PLATFORM_ANGLE_MAX_VERSION_MINOR_ANGLE);
            display_attributes.push_back(5);
            display_attributes.push_back(EGL_NONE);

            gl_ctx.egl_display = eglGetPlatformDisplay(
                EGL_PLATFORM_ANGLE_ANGLE, native_display, &display_attributes[0]);
        }
        else if (eglGetPlatformDisplayEXT)
        {
            // This path is for Windows, and no attribute has any effect so... no attributes.
            gl_ctx.egl_display =
                eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, native_display, nullptr);
        }
    }
    else
    {
        gl_ctx.egl_display = eglGetDisplay(native_display);
    }

    if (gl_ctx.egl_display == EGL_NO_DISPLAY)
    {
        HRZ_LOG_ERROR("Error getting EGL display");
        return hrz_proto::ViewerInitStatus::FRAMEBUFFER_CREATION_ERROR;
    }

    EGLint major, minor;
    if (!eglInitialize(gl_ctx.egl_display, &major, &minor))
    {
        HRZ_LOG_ERROR("Error initializing EGL");
        return hrz_proto::ViewerInitStatus::FRAMEBUFFER_CREATION_ERROR;
    }

    HRZ_LOG_INFO("Using EGL {}.{}", major, minor);

    // Once initialized, we reload EGL with the initialized display so that function pointers are up
    // to date, and some that might not have been available without a proper context, now are.
    if (!gladLoaderLoadEGL(gl_ctx.egl_display))
    {
        HRZ_LOG_ERROR("Couldn't load EGL the second time");
        return hrz_proto::ViewerInitStatus::FRAMEBUFFER_CREATION_ERROR;
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE)
    {
        return hrz_proto::ViewerInitStatus::RENDERING_API_MISSING_FEATURE_ERROR;
    }

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,
        EGL_WINDOW_BIT,
        EGL_BLUE_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_RED_SIZE,
        8,
        EGL_DEPTH_SIZE,
        0,
        EGL_CONFORMANT,
        EGL_OPENGL_ES3_BIT,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT,
        EGL_NONE};

    EGLint num_configs;
    if (eglChooseConfig(gl_ctx.egl_display, config_attribs, &gl_ctx.egl_config, 1, &num_configs)
        != EGL_TRUE)
    {
        HRZ_LOG_ERROR("EGL config selection error");
        return hrz_proto::ViewerInitStatus::FRAMEBUFFER_CREATION_ERROR;
    }
    if (num_configs == 0)
    {
        HRZ_LOG_ERROR("No EGL config available");
        return hrz_proto::ViewerInitStatus::FRAMEBUFFER_CREATION_ERROR;
    }

    EGLint surface_attribs[] = {EGL_NONE};
    gl_ctx.egl_surface = eglCreateWindowSurface(
        gl_ctx.egl_display, gl_ctx.egl_config, native_window, surface_attribs);

    if (gl_ctx.egl_surface == EGL_NO_SURFACE)
    {
        HRZ_LOG_ERROR("Could not create EGL surface");
        return hrz_proto::ViewerInitStatus::FRAMEBUFFER_CREATION_ERROR;
    }

    const EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION,
        3,
        EGL_CONTEXT_MINOR_VERSION,
        0,
        EGL_CONTEXT_OPENGL_DEBUG,
        EGL_TRUE,
        EGL_NONE};

    gl_ctx.egl_ctx =
        eglCreateContext(gl_ctx.egl_display, gl_ctx.egl_config, EGL_NO_CONTEXT, context_attribs);

    if (gl_ctx.egl_ctx == EGL_NO_CONTEXT)
    {
        auto error = eglGetError();
        HRZ_LOG_ERROR("Could not create GL context: error {:#x}", error);
        return hrz_proto::ViewerInitStatus::RENDERING_API_CONTEXT_CREATION_ERROR;
    }

    if (eglSwapInterval)
    {
        if (eglSwapInterval(gl_ctx.egl_display, 1) == EGL_TRUE)
        {
            HRZ_LOG_INFO("V-sync enabled");
        }
        else
        {
            HRZ_LOG_INFO("Couldn't enable v-sync");
        }
    }

    ctx->gl_ctx = new GlContext();
    *ctx->gl_ctx = gl_ctx;

    auto size_opt = get_current_canvas_size(ctx);
    if (size_opt)
    {
        ctx->events.push_back(Event::make_window_resized((int)size_opt->x, (int)size_opt->y));
    }

    return hrz_proto::ViewerInitStatus::INIT_SUCCESS;
}

void make_gl_ctx_current(const PlatformContext* ctx)
{
    assert(ctx && ctx->gl_ctx);
    eglMakeCurrent(
        ctx->gl_ctx->egl_display, ctx->gl_ctx->egl_surface, ctx->gl_ctx->egl_surface,
        ctx->gl_ctx->egl_ctx);
}

void swap_window(const PlatformContext* ctx)
{
    assert(ctx && ctx->gl_ctx);
    eglSwapBuffers(ctx->gl_ctx->egl_display, ctx->gl_ctx->egl_surface);
}

void cleanup_gl(PlatformContext* ctx)
{
    assert(ctx && ctx->gl_ctx);
    eglDestroyContext(ctx->gl_ctx->egl_display, ctx->gl_ctx->egl_ctx);
    delete ctx->gl_ctx;
    ctx->gl_ctx = nullptr;
}
} // namespace hrz::platform
