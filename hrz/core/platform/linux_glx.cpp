#include "hrz/core/platform/linux_x11_context.h"
#include "hrz/fnd/log.h"

#include <GL/glx.h>

namespace
{

using PFNglXCreateContextAttribsARB =
    GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
PFNglXCreateContextAttribsARB glXCreateContextAttribsARB = nullptr;

} // namespace

namespace hrz::platform
{

struct GlContext
{
    GLXContext glx_ctx;
};

hrz_proto::ViewerInitStatus initialize_gl_ctx(PlatformContext* ctx)
{
    int fb_attribs[] = {
        GLX_RENDER_TYPE,  GLX_RGBA_BIT, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
        GLX_DOUBLEBUFFER, True,         None
    };

    int fb_config_count = 0;
    GLXFBConfig* fb_configs = glXChooseFBConfig(ctx->display, 0, fb_attribs, &fb_config_count);
    if (fb_config_count == 0)
    {
        HRZ_LOG_ERROR("No suitable FB config found");
        XFree(fb_configs);
        return hrz_proto::ViewerInitStatus::FRAMEBUFFER_CREATION_ERROR;
    }

    GLXFBConfig fb_config = fb_configs[0];
    XFree(fb_configs);

    glXCreateContextAttribsARB = (PFNglXCreateContextAttribsARB)glXGetProcAddress(
        (const GLubyte*)"glXCreateContextAttribsARB");
    if (!glXCreateContextAttribsARB)
    {
        HRZ_LOG_ERROR("GLX extension GLX_ARB_create_context not present");
        return hrz_proto::ViewerInitStatus::RENDERING_API_MISSING_FEATURE_ERROR;
    }

    int attribs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB,
                     3,
                     GLX_CONTEXT_MINOR_VERSION_ARB,
                     3,
                     GLX_CONTEXT_PROFILE_MASK_ARB,
                     GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                     0};

    auto glx_ctx = glXCreateContextAttribsARB(ctx->display, fb_config, 0, True, attribs);
    if (!glx_ctx)
    {
        HRZ_LOG_ERROR("Couldn't create OpenGL Core 3.3 context");
        return hrz_proto::ViewerInitStatus::RENDERING_API_CONTEXT_CREATION_ERROR;
    }

    ctx->gl_ctx = new GlContext();
    ctx->gl_ctx->glx_ctx = glx_ctx;

    auto size_opt = get_current_canvas_size(ctx);
    if (size_opt)
    {
        ctx->events.push_back(Event::make_window_resized((int)size_opt->x, (int)size_opt->y));
    }

    return hrz_proto::ViewerInitStatus::INIT_SUCCESS;
}

GlLoadFn get_gl_load_fn()
{
    return nullptr;
}

void make_gl_ctx_current(const PlatformContext* ctx)
{
    assert(ctx && ctx->gl_ctx);
    glXMakeCurrent(ctx->display, ctx->window, ctx->gl_ctx->glx_ctx);
}

void swap_window(const PlatformContext* ctx)
{
    assert(ctx && ctx->gl_ctx);
    glXSwapBuffers(ctx->display, ctx->window);
}

void cleanup_gl(PlatformContext* ctx)
{
    assert(ctx && ctx->gl_ctx);
    glXDestroyContext(ctx->display, ctx->gl_ctx->glx_ctx);
    delete ctx->gl_ctx;
    ctx->gl_ctx = nullptr;
}

} // namespace hrz::platform
