#pragma once

#include "hrz_core_platform_events.h"

#include <hrz_fnd_variant.h>

#include <lin_maths.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace hrz
{
struct GestureSystem;

namespace gestures
{
using GestureId = uint32_t;

enum class FingerCount
{
    One,
    Two,
};

enum class GestureStatus
{
    Ongoing,
    Ended,
};

struct SingleFingerGesture
{
    enum class Type
    {
        Unqualified,
        Tap,
        DoubleTap,
        Drag,
    };

    GestureId id;
    Type type;
    GestureStatus status;
    lm::vec2 position;
    lm::vec2 initial_position;
    bool eligible_for_tap; // False for leftover digits of two-finger gestures

    /**
     * Return the distance (in pixels) between the first and the
     * current position of the finger.
     */
    float displacement_distance() const { return lm::length(position - initial_position); }

    /**
     * Return the difference between the current and the initial
     * positions.
     */
    lm::vec2 displacement() const { return position - initial_position; }

    bool is_in_viewport(int xmin, int ymin, int xmax, int ymax) const
    {
        return position.x >= xmin && position.x <= xmax && position.y >= ymin && position.y <= ymax;
    }

    SingleFingerGesture offset(int x, int y) const
    {
        SingleFingerGesture gesture = *this;
        lm::vec2 offset(x, y);
        gesture.position += offset;
        gesture.initial_position += offset;
        return gesture;
    }
};

struct TwoFingerGesture
{
    enum class Type
    {
        Unqualified,
        PinchRotate,
        Drag,
    };

    GestureId id;
    Type type;
    GestureStatus status;
    lm::vec2 positions[2];
    lm::vec2 initial_positions[2];

    /**
     * Return the mid-point between the fingers.
     */
    lm::vec2 center() const { return (positions[0] + positions[1]) * 0.5f; }

    /**
     * Return the mid-point between the fingers, when the gesture began.
     */
    lm::vec2 initial_center() const { return (initial_positions[0] + initial_positions[1]) * 0.5f; }

    /**
     * Return the distance (in pixels) between the first and the
     * current centers.
     */
    float displacement_distance() const { return lm::length(center() - initial_center()); }

    /**
     * Return the difference between the current and the initial
     * centers.
     */
    lm::vec2 displacement() const { return center() - initial_center(); }

    /**
     * Return the distance (in pixels) between the two fingers.
     */
    float spread() const { return lm::length(positions[1] - positions[0]); }

    /**
     * Return the distance (in pixels) between the two fingers, when
     * the gesture began.
     */
    float initial_spread() const { return lm::length(initial_positions[1] - initial_positions[0]); }

    /**
     * Return the ratio between the current and the initial spreads.
     */
    float spread_ratio() const
    {
        float initial = initial_spread();
        if (initial == 0) return std::numeric_limits<float>::max();
        return spread() / initial;
    }

    /**
     * Return the signed angle (in radians) between the line passing
     * through the two fingers and a horizontal line.
     */
    float orientation() const
    {
        auto axis = positions[1] - positions[0];
        return std::atan2(axis.y, axis.x);
    }

    /**
     * Return the signed angle (in radians) between the line passing
     * through the two fingers and a horizontal line, when the gesture
     * began.
     */
    float initial_orientation() const
    {
        auto axis = initial_positions[1] - initial_positions[0];
        return std::atan2(axis.y, axis.x);
    }

    /**
     * Return the angle difference (in radians) for the line passing
     * through the two fingers, between the initial positions and the
     * current ones.
     */
    float rotation() const { return orientation() - initial_orientation(); }

    bool is_in_viewport(int xmin, int ymin, int xmax, int ymax) const
    {
        return positions[0].x >= xmin && positions[0].x <= xmax && positions[0].y >= ymin
            && positions[0].y <= ymax && positions[1].x >= xmin && positions[1].x <= xmax
            && positions[1].y >= ymin && positions[1].y <= ymax;
    }

    TwoFingerGesture offset(int x, int y) const
    {
        TwoFingerGesture gesture = *this;
        lm::vec2 offset(x, y);
        gesture.positions[0] += offset;
        gesture.positions[1] += offset;
        gesture.initial_positions[0] += offset;
        gesture.initial_positions[1] += offset;
        return gesture;
    }
};

struct Event
{
    enum class Kind
    {
        New,
        Move,
        Qualification,
        End,
    };

    using GestureVariant =
        std::variant<hrz::gestures::SingleFingerGesture, hrz::gestures::TwoFingerGesture>;
    static constexpr size_t SingleFinger =
        hrz::index_of_variant<GestureVariant, SingleFingerGesture>();
    static constexpr size_t TwoFinger = hrz::index_of_variant<GestureVariant, TwoFingerGesture>();

    Kind kind;
    GestureVariant gesture;

    // This is used on New events. It gives the previous finger count before this event.
    // For example, when it's a brand new gesture, this is 0. When it's a single finger event
    // caused by removing the finger of a two-finger event, this is 2, etc.
    int previous_finger_count{};

    FingerCount finger_count() const
    {
        return gesture.index() == SingleFinger ? FingerCount::One : FingerCount::Two;
    }

    const hrz::gestures::SingleFingerGesture& single_finger_gesture() const
    {
        return std::get<hrz::gestures::SingleFingerGesture>(gesture);
    }

    const hrz::gestures::TwoFingerGesture& two_finger_gesture() const
    {
        return std::get<hrz::gestures::TwoFingerGesture>(gesture);
    }

    bool is_in_viewport(int xmin, int ymin, int xmax, int ymax) const
    {
        switch (gesture.index())
        {
            case SingleFinger:
                return single_finger_gesture().is_in_viewport(xmin, ymin, xmax, ymax);
            case TwoFinger: return two_finger_gesture().is_in_viewport(xmin, ymin, xmax, ymax);
            default: return false;
        }
    }

    Event offset(int x, int y) const
    {
        Event e = *this;
        switch (gesture.index())
        {
            case SingleFinger: e.gesture = e.single_finger_gesture().offset(x, y); break;
            case TwoFinger: e.gesture = e.two_finger_gesture().offset(x, y); break;
            default: break;
        }

        return e;
    }
};

/**
 * Create a gesture system.
 */
GestureSystem* create_system();

/**
 * Destroy a gesture system.
 */
void destroy_system(GestureSystem*);

/**
 * Pass in platform events to the gesture system.
 * Gestures are created from the touch events.
 */
bool handle_platform_event(GestureSystem*, const platform::Event&);

/**
 * Retrieve a single-finger gesture by its ID.
 */
std::optional<SingleFingerGesture> get_single_finger_gesture(const GestureSystem*, GestureId id);

/**
 * Retrieve a two-finger gesture by its ID.
 */
std::optional<TwoFingerGesture> get_two_finger_gesture(const GestureSystem*, GestureId id);

/**
 * Remove the ended gestures from the system.
 * Call this function after the systems that use gestures
 * have been updated, so that they can react to the last
 * position of the gestures, and possibly on their ended
 * state.
 */
void remove_ended_gestures(GestureSystem*);

/**
 * Remove the oldest gesture event from the queue and return it.
 */
bool dequeue_event(GestureSystem*, Event&);
} // namespace gestures
} // namespace hrz
