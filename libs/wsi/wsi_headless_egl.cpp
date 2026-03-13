#ifdef __linux__

#    include "wsi.h"

#    include <glad/egl.h>

#    include <cstdio>
#    include <cstdlib>
#    include <cstring>

// See https://developer.nvidia.com/blog/egl-eye-opengl-visualization-without-x-server/

namespace
{
EGLDisplay display = EGL_NO_DISPLAY;
EGLSurface surface_;
} // namespace

std::optional<WsiInstance> wsi_init(int width, int height, const char*, bool)
{
    if (!gladLoaderLoadEGL(nullptr))
    {
        printf("Error: Couldn't load EGL\n");
        return std::nullopt;
    }

    const char* extension_string =
        static_cast<const char*>(eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));
    if (!extension_string)
    {
        // Fallback to an empty string for strstr()
        extension_string = "";
    }

    if (strstr(extension_string, "EGL_EXT_device_query")
        && strstr(extension_string, "EGL_EXT_platform_device"))
    {
        // Simply calling `eglGetDisplay(EGL_DEFAULT_DISPLAY)` does not work on some
        // platforms, including Mesa, so we have to list devices, take the first one,
        // and then create the display for it.

        EGLint num_devices;
        eglQueryDevicesEXT(0, NULL, &num_devices);

        if (num_devices == 0)
        {
            printf("Error: No EGL compatible devices found\n");
            return std::nullopt;
        }

        num_devices = 1;
        EGLDeviceEXT device;
        eglQueryDevicesEXT(num_devices, &device, &num_devices);

        if (eglGetPlatformDisplay)
        {
            display = eglGetPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, device, 0);
        }
        else if (eglGetPlatformDisplayEXT)
        {
            display = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, device, 0);
        }
    }

    if (display == EGL_NO_DISPLAY)
    {
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }

    EGLint major, minor;

    auto try_initialize = [&]()
    {
        if (eglInitialize(display, &major, &minor) != EGL_TRUE)
        {
            printf("EGL initialization error: ");
            auto error = eglGetError();
            if (error == EGL_NOT_INITIALIZED)
            {
                printf("EGL_NOT_INITIALIZED\n");
            }
            else if (error == EGL_BAD_DISPLAY)
            {
                printf("EGL_BAD_DISPLAY\n");
            }
            else
            {
                printf("unknown error\n");
            }

            return false;
        }
        return true;
    };

    if (!try_initialize())
    {
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (!try_initialize())
        {
            return std::nullopt;
        }
    }

    if (!gladLoaderLoadEGL(display))
    {
        printf("Error: Couldn't load EGL\n");
        return std::nullopt;
    }

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,
        EGL_PBUFFER_BIT,
        EGL_BLUE_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_RED_SIZE,
        8,
        EGL_DEPTH_SIZE,
        8,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_BIT,
        EGL_NONE};

    EGLint num_configs;
    EGLConfig config;
    if (eglChooseConfig(display, config_attribs, &config, 1, &num_configs) != EGL_TRUE)
    {
        printf("EGL config selection error\n");
        return std::nullopt;
    }
    if (num_configs == 0)
    {
        printf("Error: No EGL config available\n");
        return std::nullopt;
    }

    const EGLint pbuffer_attribs[] = {
        EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE,
    };

    surface_ = eglCreatePbufferSurface(display, config, pbuffer_attribs);

    if (surface_ == EGL_NO_SURFACE)
    {
        printf("Error: Could not create EGL surface\n");
        return std::nullopt;
    }

    return WsiInstance{(void*)&display, (void*)&surface_};
}

void wsi_run(WsiRunFn fn)
{
    while (fn())
    {
    }
}

void wsi_cleanup()
{
    eglTerminate(display);
}

#endif
