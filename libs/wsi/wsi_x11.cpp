#ifdef __linux__

#    include "wsi.h"

#    include <hrz_icons.h>

#    include <X11/Xlib.h>
#    include <X11/Xutil.h>
#    include <assert.h>
#    include <gsl/gsl-lite.hpp>
#    include <stb_image.h>
#    include <unistd.h>

#    include <stdio.h>

#    define RES_NAME "horizon-test-client"
#    define RES_CLASS "Horizon Test Client"

#    define HRZ_VERSION_STR3(X) #X
#    define HRZ_VERSION_STR2(X) HRZ_VERSION_STR3(X)
#    define HRZ_VERSION_STR HRZ_VERSION_STR2(HRZ_VERSION)

namespace
{
Display* display = nullptr;
int screen;
Window window = {};

// https://stackoverflow.com/a/15595582
gsl::span<unsigned long> create_icon_from_bytes(int width, int height, const unsigned char* bytes)
{
    size_t data_size = 2 + width * height;
    auto data = (unsigned long*)malloc(data_size * sizeof(unsigned long));
    data[0] = width;
    data[1] = height;

    for (size_t i = 0; i < (size_t)(width * height); ++i)
    {
        // stb_image outputs RGBA, Xlib wants ARGB.
        unsigned long pixel = ((unsigned long)bytes[i * 4 + 0] << 16)
            | ((unsigned long)bytes[i * 4 + 1] << 8) | ((unsigned long)bytes[i * 4 + 2] << 0)
            | ((unsigned long)bytes[i * 4 + 3] << 24);
        data[i + 2] = pixel;
    }

    return {data, data_size};
}
} // namespace

static_assert(sizeof(Window) == sizeof(void*), "Wrong size for Window struct");

std::optional<WsiInstance> wsi_init(int width, int height, bool show_window)
{
    display = XOpenDisplay(nullptr);
    assert(display);

    screen = DefaultScreen(display);
    window = XCreateSimpleWindow(
        display, RootWindow(display, screen), 10, 10, width, height, 1, BlackPixel(display, screen),
        WhitePixel(display, screen));
    XStoreName(display, window, "Horizon " HRZ_VERSION_STR);

    char res_name[sizeof(RES_NAME)] = RES_NAME;
    char res_class[sizeof(RES_CLASS)] = RES_CLASS;
    auto class_hint = XAllocClassHint();
    if (class_hint)
    {
        // Cannot use string litterals here, because the XClassHint
        // structure takes pointers to mutable data.
        class_hint->res_name = res_name;
        class_hint->res_class = res_class;
    }
    XSetClassHint(display, window, class_hint);
    XFree(class_hint);

    auto icon_res = hrz_icons::get_data(hrz_icons::Resources::Icon96);
    int icon_width;
    int icon_height;
    int channels_in_icon_file;
    stbi_uc* decoded_icon_data = stbi_load_from_memory(
        (const stbi_uc*)icon_res.data(), (int)icon_res.size_bytes(), &icon_width, &icon_height,
        &channels_in_icon_file, 4);

    if (decoded_icon_data)
    {
        auto wm_icon_data = create_icon_from_bytes(icon_width, icon_height, decoded_icon_data);
        stbi_image_free(decoded_icon_data);

        Atom net_wm_icon = XInternAtom(display, "_NET_WM_ICON", False);
        Atom cardinal = XInternAtom(display, "CARDINAL", False);
        XChangeProperty(
            display, window, net_wm_icon, cardinal, 32, PropModeReplace,
            (const unsigned char*)wm_icon_data.data(), wm_icon_data.size());

        free(wm_icon_data.data());
    }

    if (show_window)
    {
        XMapWindow(display, window);
    }

    return {WsiInstance{(void*)display, (void*)window}};
}

void wsi_run(WsiRunFn fn)
{
    while (fn())
        ;
}

void wsi_cleanup()
{
    XCloseDisplay(display);
}

#endif
