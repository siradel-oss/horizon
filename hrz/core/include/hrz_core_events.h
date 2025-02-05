#pragma once

#include "hrz_core_gestures.h"
#include "hrz_core_platform_events.h"

namespace hrz
{
struct Event
{
    enum class Kind
    {
        ModKeyDown,
        ModKeyUp,
        Platform,
        Gesture
    };

    Kind kind;

    union
    {
        platform::Event platform;
        gestures::Event gesture;
    };

    bool _is_in_viewport(int xmin, int ymin, int xmax, int ymax) const
    {
        switch (kind)
        {
            case Kind::Platform: return platform.is_in_viewport(xmin, ymin, xmax, ymax);
            case Kind::Gesture: return gesture.is_in_viewport(xmin, ymin, xmax, ymax);
            default: break;
        }
        return true;
    }

    Event _offset(int x, int y) const
    {
        switch (kind)
        {
            case Kind::Platform: return make_platform(platform.offset(x, y));
            case Kind::Gesture: return make_gesture(gesture.offset(x, y));
            default: break;
        }
        return *this;
    }

    static Event make_platform(const platform::Event& platform)
    {
        Event e = {Kind::Platform, platform};
        e.platform = platform;

        return e;
    }

    static Event make_gesture(const gestures::Event& gesture)
    {
        Event e = {Kind::Gesture};
        e.gesture = gesture;

        return e;
    }
};

struct ViewportEvent : public Event
{
    bool is_in_viewport;

    ViewportEvent(const Event& e, lm::ibbox2 viewport) :
        Event(e._offset(-viewport.min.x, -viewport.min.y)),
        is_in_viewport(
            e._is_in_viewport(viewport.min.x, viewport.min.y, viewport.max.x, viewport.max.y))
    {
    }
};

static constexpr bool is_platform_event(const Event& e, platform::Event::Kind kind)
{
    return e.kind == Event::Kind::Platform && e.platform.kind == kind;
}

static constexpr bool is_key_down_event(const Event& e, platform::Event::Key key)
{
    return is_platform_event(e, platform::Event::Kind::KeyDown) && e.platform.key == key;
}

static constexpr bool is_key_up_event(const Event& e, platform::Event::Key key)
{
    return is_platform_event(e, platform::Event::Kind::KeyUp) && e.platform.key == key;
}

static constexpr bool is_mouse_button_down_event(
    const Event& e,
    platform::Event::MouseButton button)
{
    return is_platform_event(e, platform::Event::Kind::MouseButtonDown)
        && e.platform.mouse_button.button == button;
}

static constexpr bool is_mouse_button_up_event(const Event& e, platform::Event::MouseButton button)
{
    return is_platform_event(e, platform::Event::Kind::MouseButtonUp)
        && e.platform.mouse_button.button == button;
}

static constexpr bool is_mouse_wheel_event(const Event& e)
{
    return is_platform_event(e, platform::Event::Kind::MouseWheel);
}

static constexpr bool is_single_gesture_event(const Event& e, gestures::Event::Kind kind)
{
    return e.kind == Event::Kind::Gesture && e.gesture.finger_count() == gestures::FingerCount::One
        && e.gesture.kind == kind;
}

static constexpr bool is_single_finger_gesture_start_event(const Event& e)
{
    return is_single_gesture_event(e, gestures::Event::Kind::New)
        && e.gesture.previous_finger_count == 0;
}

static constexpr bool is_single_finger_gesture_qualification_event(
    const Event& e,
    gestures::GestureId id,
    gestures::SingleFingerGesture::Type type)
{
    return is_single_gesture_event(e, gestures::Event::Kind::Qualification)
        && e.gesture.single_finger_gesture().type == type
        && e.gesture.single_finger_gesture().id == id;
}

static constexpr bool is_single_finger_gesture_end_event(const Event& e, gestures::GestureId id)
{
    return is_single_gesture_event(e, gestures::Event::Kind::End)
        && e.gesture.single_finger_gesture().id == id;
}

static constexpr bool is_single_finger_gesture_move_event(const Event& e, gestures::GestureId id)
{
    return is_single_gesture_event(e, gestures::Event::Kind::Move)
        && e.gesture.single_finger_gesture().id == id;
}

static constexpr bool is_two_gesture_event(const Event& e, gestures::Event::Kind kind)
{
    return e.kind == Event::Kind::Gesture && e.gesture.finger_count() == gestures::FingerCount::Two
        && e.gesture.kind == kind;
}

static constexpr bool is_two_finger_gesture_qualification_event(
    const Event& e,
    gestures::TwoFingerGesture::Type type)
{
    return is_two_gesture_event(e, gestures::Event::Kind::Qualification)
        && e.gesture.two_finger_gesture().type == type;
}

static constexpr bool is_two_finger_gesture_end_event(const Event& e, gestures::GestureId id)
{
    return is_two_gesture_event(e, gestures::Event::Kind::End)
        && e.gesture.two_finger_gesture().id == id;
}

static constexpr bool is_two_finger_gesture_move_event(const Event& e, gestures::GestureId id)
{
    return is_two_gesture_event(e, gestures::Event::Kind::Move)
        && e.gesture.two_finger_gesture().id == id;
}

} // namespace hrz
