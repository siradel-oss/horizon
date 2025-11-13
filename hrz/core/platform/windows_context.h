#pragma once

#include "hrz/core/platform/platform.h"
#include "hrz/fnd/flat_hash_set.h"

#include <windows.h>

#include <deque>

namespace hrz
{
namespace platform
{
struct GlContext;

void cleanup_gl(PlatformContext*);
} // namespace platform

struct PlatformContext
{
    platform::GlContext* gl_ctx;

    HWND hwnd;
    HINSTANCE hinstance;
    bool enable_events_capture;
    bool ignore_user_interaction_events;
    std::deque<platform::Event> events;

    int mouse_x = 0;
    int mouse_y = 0;
    int old_width = 0;
    int old_height = 0;
    int new_width = 0;
    int new_height = 0;

    hrz::flat_hash_set<platform::Event::MouseButton> down_mouse_buttons;
    hrz::flat_hash_set<platform::Event::Key> down_keys;

    void release_all_mouse_down()
    {
        for (auto button : down_mouse_buttons)
        {
            events.push_back(platform::Event::make_mouse_button_up(button, mouse_x, mouse_y));
        }
        down_mouse_buttons.clear();
    }

    void release_all_key_down()
    {
        for (auto key : down_keys)
        {
            events.push_back(platform::Event::make_key_up(key));
        }
        down_keys.clear();
    }

    void release_all_down()
    {
        release_all_mouse_down();
        release_all_key_down();
    }
};

} // namespace hrz
