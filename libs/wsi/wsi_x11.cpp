// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#ifdef __linux__

#    include "wsi.h"

#    include <X11/Xlib.h>
#    include <X11/Xutil.h>
#    include <unistd.h>

#    include <cassert>
#    include <span>
#    include <stdio.h>

#    define RES_NAME "horizon-test-client"
#    define RES_CLASS "Horizon Test Client"

namespace
{

Display* display = nullptr;
int screen;
Window window = {};

} // namespace

static_assert(sizeof(Window) == sizeof(void*), "Wrong size for Window struct");

std::optional<WsiInstance> wsi_init(
    int width,
    int height,
    const char* window_title,
    bool show_window)
{
    display = XOpenDisplay(nullptr);
    assert(display);

    screen = DefaultScreen(display);
    window = XCreateSimpleWindow(
        display, RootWindow(display, screen), 10, 10, width, height, 1, BlackPixel(display, screen),
        WhitePixel(display, screen));
    XStoreName(display, window, window_title);

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
