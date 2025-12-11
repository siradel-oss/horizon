#pragma once

#include "hrz/core/platform/platform.h"
#include "hrz/fnd/flat_hash_set.h"

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>

namespace hrz
{
namespace platform
{
struct GlContext;

void cleanup_gl(PlatformContext*);
} // namespace platform

struct PlatformContext
{
    Display* display;
    Window window;
    std::optional<int> xi_opcode;
    platform::GlContext* gl_ctx;
    std::deque<platform::Event> events;
    Atom close_window_atom;
    int width;
    int height;
    int mouse_x = 0;
    int mouse_y = 0;

    struct DoubleClickInfo
    {
        double start_time_ms;
        int start_mouse_x;
        int start_mouse_y;
        platform::Event::MouseButton button;

    } dblclk_info;

    // We have to use this to detect key repeats with X11...
    uint32_t key_mask[8];

    inline bool is_key_pressed(uint8_t k)
    {
        return (key_mask[k >> 5] & ((uint32_t)1 << (k & 0x1f))) != 0;
    }

    inline void press_key(uint8_t k) { key_mask[k >> 5] |= (uint32_t)1 << (k & 0x1f); }

    inline void release_key(uint8_t k) { key_mask[k >> 5] &= ~((uint32_t)1 << (k & 0x1f)); }

    hrz::flat_hash_set<platform::Event::MouseButton> down_mouse_buttons;
    hrz::flat_hash_set<platform::Event::Key> down_keys;

    Window clipboard_window;
    Atom clipboard_atom;
    Atom targets_atom;
    Atom utf8_string_atom;
    Atom text_plain_utf8_atom;
    Atom text_plain_atom;
    Atom text_atom;
    Atom hrz_clipboard_atom;

    bool enable_events_capture;
    bool ignore_user_interaction_events;

    enum class ClipboardMimeType
    {
        UTF8_STRING,
        TEXT_PLAIN_UTF8,
        TEXT_PLAIN,
        TEXT,
    };
    static constexpr size_t ClipboardMimeTypeCount = 4;

    Atom get_clipboard_mime_type_atom(ClipboardMimeType mime_type)
    {
        switch (mime_type)
        {
            case ClipboardMimeType::UTF8_STRING: return utf8_string_atom;
            case ClipboardMimeType::TEXT_PLAIN_UTF8: return text_plain_utf8_atom;
            case ClipboardMimeType::TEXT_PLAIN: return text_plain_atom;
            case ClipboardMimeType::TEXT: return text_atom;
            default: assert(false && "Unhandled case"); return utf8_string_atom;
        }
    }
};

static_assert(sizeof(Window) == sizeof(void*), "Wrong size for Window struct");

} // namespace hrz
