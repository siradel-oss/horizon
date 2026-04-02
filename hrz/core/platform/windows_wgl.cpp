#include "hrz/core/platform/windows_context.h"
#include "hrz/fnd/log.h"

#include <assert.h>
#include <windows.h>
#include <windowsx.h>

// Here we define some stuff from the WGL_ARB_create_context extension that
// we'll need to create a core OpenGL context.
namespace
{

#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

using PFNwglCreateContextAttribsARB =
    HGLRC(WINAPI*)(HDC hDC, HGLRC hShareContext, const int* attribList);
PFNwglCreateContextAttribsARB wglCreateContextAttribsARB = nullptr;

using PFNwglSwapIntervalEXT = BOOL(WINAPI*)(int interval);
PFNwglSwapIntervalEXT wglSwapIntervalEXT = nullptr;

} // namespace

namespace hrz
{

namespace platform
{

struct GlContext
{
    HDC hdc;
    HGLRC glrc;
};

hrz_proto::ViewerInitStatus initialize_gl_ctx(PlatformContext* ctx)
{
    assert(ctx);

    PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR),
        1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_SUPPORT_COMPOSITION,
        PFD_TYPE_RGBA,
        24, // Color
        8,  // Red
        8,  // Green
        8,  // Blue
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0, // Depth
        0, // Stencil
        0,
        PFD_MAIN_PLANE,
        0,
        0,
        0,
        0
    };

    HDC hdc = GetDC(ctx->hwnd);
    if (!hdc)
    {
        HRZ_LOG_ERROR("Unable to fetch device context");
        return hrz_proto::ViewerInitStatus::FRAMEBUFFER_CREATION_ERROR;
    }

    int pf_index = ChoosePixelFormat(hdc, &pfd);
    if (pf_index == 0)
    {
        HRZ_LOG_ERROR("No available pixel format found");
        ReleaseDC(ctx->hwnd, hdc);
        return hrz_proto::ViewerInitStatus::FRAMEBUFFER_CREATION_ERROR;
    }

    if (!SetPixelFormat(hdc, pf_index, &pfd))
    {
        HRZ_LOG_ERROR("Unsable to set pixel format");
        ReleaseDC(ctx->hwnd, hdc);
        return hrz_proto::ViewerInitStatus::FRAMEBUFFER_CREATION_ERROR;
    }

    // We create a temporary context to we can use wglGetProcAddress to query the
    // function to create a core OpenGL context.
    HGLRC glrc_tmp = wglCreateContext(hdc);
    if (!glrc_tmp)
    {
        HRZ_LOG_ERROR("Couldn't create temporary OpenGL context");
        ReleaseDC(ctx->hwnd, hdc);
        return hrz_proto::ViewerInitStatus::RENDERING_API_CONTEXT_CREATION_ERROR;
    }

    wglMakeCurrent(hdc, glrc_tmp);

    wglCreateContextAttribsARB =
        (PFNwglCreateContextAttribsARB)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!wglCreateContextAttribsARB)
    {
        HRZ_LOG_ERROR("WGL extension WGL_ARB_create_context not present");
        ReleaseDC(ctx->hwnd, hdc);
        return hrz_proto::ViewerInitStatus::RENDERING_API_MISSING_FEATURE_ERROR;
    }

    int ctx_attribs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
                         3,
                         WGL_CONTEXT_MINOR_VERSION_ARB,
                         3,
                         WGL_CONTEXT_PROFILE_MASK_ARB,
                         WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                         0};

    HGLRC glrc = wglCreateContextAttribsARB(hdc, nullptr, ctx_attribs);
    if (!glrc)
    {
        HRZ_LOG_ERROR("Couldn't create OpenGL Core 3.3 context");
        ReleaseDC(ctx->hwnd, hdc);
        return hrz_proto::ViewerInitStatus::RENDERING_API_CONTEXT_CREATION_ERROR;
    }

    wglSwapIntervalEXT = (PFNwglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT");
    if (wglSwapIntervalEXT)
    {
        if (wglSwapIntervalEXT(1))
        {
            HRZ_LOG_INFO("V-sync enabled");
        }
        else
        {
            HRZ_LOG_INFO("Couldn't enable v-sync");
        }
    }
    else
    {
        HRZ_LOG_INFO("WGL_EXT_swap_control extension not available");
    }

    ctx->gl_ctx = new GlContext();
    ctx->gl_ctx->glrc = glrc;
    ctx->gl_ctx->hdc = hdc;

    auto size_opt = get_current_canvas_size(ctx);
    if (size_opt)
    {
        ctx->events.push_back(Event::make_window_resized((int)size_opt->x, (int)size_opt->y));
    }

    wglDeleteContext(glrc_tmp);
    return hrz_proto::ViewerInitStatus::INIT_SUCCESS;
}

GlLoadFn get_gl_load_fn()
{
    return nullptr;
}

void make_gl_ctx_current(const PlatformContext* ctx)
{
    assert(ctx);

    if (wglGetCurrentDC() != ctx->gl_ctx->hdc || wglGetCurrentContext() != ctx->gl_ctx->glrc)
    {
        wglMakeCurrent(ctx->gl_ctx->hdc, ctx->gl_ctx->glrc);
    }
}

void swap_window(const PlatformContext* ctx)
{
    assert(ctx);
    if (!SwapBuffers(ctx->gl_ctx->hdc))
    {
        HRZ_LOG_ERROR("Couldn't properly swap buffers");
    }
}

void cleanup_gl(PlatformContext* ctx)
{
    assert(ctx);

    if (ctx->gl_ctx->glrc)
    {
        wglDeleteContext(ctx->gl_ctx->glrc);
    }

    delete ctx->gl_ctx;
}

} // namespace platform
} // namespace hrz
