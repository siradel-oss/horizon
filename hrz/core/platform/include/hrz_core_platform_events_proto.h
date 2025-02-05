#pragma once

#include "hrz_core_platform_events.h"

#include <hrz_protocol_all.h>

namespace hrz::platform
{
static Event::Key from_proto(const hrz_proto::Key& key)
{
    return (Event::Key)key;
}

static Event from_proto(const hrz_proto::Event& event, float device_pixel_ratio)
{
    switch (event.kind_case())
    {
        case hrz_proto::Event::kWindowClosed: return Event::make_window_closed();
        case hrz_proto::Event::kWindowResized:
            return Event::make_window_resized(
                event.window_resized().width() * device_pixel_ratio,
                event.window_resized().height() * device_pixel_ratio);
        case hrz_proto::Event::kCharacter:
            return Event::make_character(
                (char)event.character().char_(), event.character().repeat());
        case hrz_proto::Event::kKeyDown:
            return Event::make_key_down((Event::Key)event.key_down().key());
        case hrz_proto::Event::kKeyUp: return Event::make_key_up((Event::Key)event.key_up().key());
        case hrz_proto::Event::kMouseMove:
            return Event::make_mouse_move(
                event.mouse_move().x() * device_pixel_ratio,
                event.mouse_move().y() * device_pixel_ratio,
                event.mouse_move().dx() * device_pixel_ratio,
                event.mouse_move().dy() * device_pixel_ratio);
        case hrz_proto::Event::kMouseWheel:
            return Event::make_mouse_wheel(
                event.mouse_wheel().x() * device_pixel_ratio,
                event.mouse_wheel().y() * device_pixel_ratio, event.mouse_wheel().wheel());
        case hrz_proto::Event::kMouseButtonDown:
            return Event::make_mouse_button_down(
                (Event::MouseButton)event.mouse_button_down().button(),
                event.mouse_button_down().x() * device_pixel_ratio,
                event.mouse_button_down().y() * device_pixel_ratio);
        case hrz_proto::Event::kMouseButtonUp:
            return Event::make_mouse_button_up(
                (Event::MouseButton)event.mouse_button_up().button(),
                event.mouse_button_up().x() * device_pixel_ratio,
                event.mouse_button_up().y() * device_pixel_ratio);
        case hrz_proto::Event::kMouseButtonDoubleClick:
            return Event::make_mouse_button_double_click(
                (Event::MouseButton)event.mouse_button_double_click().button(),
                event.mouse_button_double_click().x() * device_pixel_ratio,
                event.mouse_button_double_click().y() * device_pixel_ratio);
        case hrz_proto::Event::kMouseLeave: return Event::make_mouse_leave();
        case hrz_proto::Event::kTouchStart:
            return Event::make_touch_start(
                event.touch_start().id(), event.touch_start().x() * device_pixel_ratio,
                event.touch_start().y() * device_pixel_ratio);
        case hrz_proto::Event::kTouchEnd:
            return Event::make_touch_end(
                event.touch_end().id(), event.touch_end().x() * device_pixel_ratio,
                event.touch_end().y() * device_pixel_ratio);
        case hrz_proto::Event::kTouchMove:
            return Event::make_touch_move(
                event.touch_move().id(), event.touch_move().x() * device_pixel_ratio,
                event.touch_move().y() * device_pixel_ratio);
        case hrz_proto::Event::kTouchCancel:
            return Event::make_touch_cancel(
                event.touch_cancel().id(), event.touch_cancel().x() * device_pixel_ratio,
                event.touch_cancel().y() * device_pixel_ratio);
        default: return Event{};
    }
}

} // namespace hrz::platform
