
#include "hrz_core_platform.h"

#include <glad/egl.h>

#include <cassert>
#include <deque>

extern "C"
{
#include <microui/microui.h>
}

namespace hrz
{
struct PlatformContext
{
    EGLDisplay display;
    EGLConfig config;
    EGLSurface surface;

    EGLContext gl_ctx;

    std::deque<platform::Event> events;
};

namespace platform
{
PlatformContext* initialize(
    bool disable_events_capture,
    void* wsi_instance,
    void* wsi_window,
    const char* canvas_selector,
    float device_pixel_ratio_override)
{
    assert(wsi_instance);
    assert(wsi_window);

    PlatformContext* ctx = new PlatformContext;
    ctx->display = *(EGLDisplay*)wsi_instance;
    ctx->surface = *(EGLSurface*)wsi_window;

    return ctx;
}

hrz_proto::ViewerInitStatus initialize_gl_ctx(PlatformContext* ctx)
{
    assert(ctx);

    if (!gladLoaderLoadEGL(nullptr) || !gladLoaderLoadEGL(ctx->display))
    {
        return hrz_proto::ViewerInitStatus::RENDERING_API_CONTEXT_CREATION_ERROR;
    }

    EGLint config_id;
    if (eglQuerySurface(ctx->display, ctx->surface, EGL_CONFIG_ID, &config_id) != EGL_TRUE)
    {
        return hrz_proto::ViewerInitStatus::RENDERING_API_CONTEXT_CREATION_ERROR;
    }

    const EGLint config_attribs[] = {EGL_CONFIG_ID, config_id, EGL_NONE};

    EGLint num_configs;
    if (eglChooseConfig(ctx->display, config_attribs, &ctx->config, 1, &num_configs) != EGL_TRUE)
    {
        return hrz_proto::ViewerInitStatus::RENDERING_API_CONTEXT_CREATION_ERROR;
    }

    if (num_configs == 0)
    {
        return hrz_proto::ViewerInitStatus::RENDERING_API_CONTEXT_CREATION_ERROR;
    }

    if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE)
    {
        return hrz_proto::ViewerInitStatus::RENDERING_API_CONTEXT_CREATION_ERROR;
    }

    const EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_NONE};

    EGLContext gl_ctx =
        eglCreateContext(ctx->display, ctx->config, EGL_NO_CONTEXT, context_attribs);

    if (gl_ctx == EGL_NO_CONTEXT)
    {
        return hrz_proto::ViewerInitStatus::RENDERING_API_CONTEXT_CREATION_ERROR;
    }

    ctx->gl_ctx = gl_ctx;

    auto size_opt = get_current_canvas_size(ctx);
    if (size_opt)
    {
        ctx->events.push_back(
            platform::Event::make_window_resized((int)size_opt->x, (int)size_opt->y));
    }
    else
    {
        return hrz_proto::ViewerInitStatus::RENDERING_API_CONTEXT_CREATION_ERROR;
    }

    return hrz_proto::ViewerInitStatus::INIT_SUCCESS;
}

std::optional<lm::uvec2> get_current_canvas_size(const PlatformContext* ctx)
{
    EGLint width, height;
    if (eglQuerySurface(ctx->display, ctx->surface, EGL_WIDTH, &width) != EGL_TRUE
        || eglQuerySurface(ctx->display, ctx->surface, EGL_HEIGHT, &height) != EGL_TRUE)
    {
        return std::nullopt;
    }
    else
    {
        return lm::uvec2(width, height);
    }
}

GlLoadFn get_gl_load_fn()
{
    return (GlLoadFn)eglGetProcAddress;
}

void make_gl_ctx_current(const PlatformContext* ctx)
{
    assert(ctx);

    eglMakeCurrent(ctx->display, ctx->surface, ctx->surface, ctx->gl_ctx);
}

void swap_window(const PlatformContext* ctx)
{
    assert(ctx);

    eglSwapBuffers(ctx->display, ctx->surface);
}

void advance_events(PlatformContext*) {}

bool dequeue_event(PlatformContext* ctx, Event* event)
{
    assert(ctx);

    if (!ctx->events.empty())
    {
        *event = ctx->events.front();
        ctx->events.pop_front();
        return true;
    }
    else
    {
        return false;
    }
}

void set_user_interactions_enabled(PlatformContext*, bool) {}

void cleanup(PlatformContext* ctx)
{
    assert(ctx);

    delete ctx;
}

void copy_to_clipboard(PlatformContext*, std::string_view) {}

void add_key_to_capture(PlatformContext*, hrz::platform::Event::Key) {}

void add_key_bypassing_focus(PlatformContext*, hrz::platform::Event::Key) {}

float get_current_device_pixel_ratio(const PlatformContext* ctx)
{
    return 1.0f;
}

void viewport_dev_ui(PlatformContext* ctx, mu_Context* ui) {}
} // namespace platform
} // namespace hrz
