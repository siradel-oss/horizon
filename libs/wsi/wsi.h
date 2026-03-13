#pragma once

#include <optional>

// Returns whether the window should stay open or not.
using WsiRunFn = bool (*)();

struct WsiInstance
{
    void* instance;
    void* window;
};

std::optional<WsiInstance> wsi_init(
    int width,
    int height,
    const char* window_title,
    bool show_window = true);
void wsi_run(WsiRunFn fn);
void wsi_cleanup();
