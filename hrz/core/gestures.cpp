#include "hrz/core/gestures.h"

#include "hrz/fnd/defines.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/time.h"

#include <cassert>
#include <deque>
#include <set>

namespace
{
static constexpr float MAX_DISTANCE_FOR_TAP = 15.0f; // pixels
static constexpr double MAX_DURATION_FOR_TAP = 400;  // ms
#ifndef HRZ_EMSCRIPTEN
static constexpr float MAX_DISTANCE_FOR_DOUBLE_TAP = 40.0f; // pixels
static constexpr double MAX_DURATION_FOR_DOUBLE_TAP = 800;  // ms
#endif

static constexpr float DISTANCE_FOR_SINGLE_FINGER_DRAG = 15.0f; // pixels
static constexpr float DISTANCE_FOR_TWO_FINGER_DRAG = 40.0f;    // pixels
static constexpr float ANGLE_FOR_ROTATION = 0.2;                // radians
static constexpr float SPREAD_GROW_RATIO_FOR_PINCH = 1.1f;
static constexpr float SPREAD_SHRINK_RATIO_FOR_PINCH = 0.9f;
static constexpr double ENDED_GESTURE_TTL = 500; // ms

float angular_difference(float a, float b)
{
    float diff = a - b;
    while (diff > lm::PIf * 0.5f)
        diff -= lm::PIf;
    while (diff < -lm::PIf * 0.5f)
        diff += lm::PIf;
    return diff;
}
} // namespace

namespace
{
struct Gesture
{
    hrz::gestures::FingerCount finger_count;
    int touch_ids[2];
    std::variant<hrz::gestures::SingleFingerGesture, hrz::gestures::TwoFingerGesture> gesture;
    double start_time_ms;
    double end_time_ms;
};
} // namespace

namespace hrz
{
struct GestureSystem
{
    using IndexPool = GenIndexPool<gestures::GestureId, 16, 16>;
    using GesturePool = GenObjectPool<Gesture, IndexPool, 128>;

    GesturePool gesture_pool;
    std::set<gestures::GestureId> gestures;

    std::deque<gestures::Event> events;
};

namespace gestures
{
GestureSystem* create_system()
{
    return new GestureSystem();
}

void destroy_system(GestureSystem* system)
{
    assert(system);

    delete system;
}

namespace
{
GestureStatus get_status(const Gesture* gesture)
{
    if (gesture->finger_count == FingerCount::One)
    {
        const auto& single_finger_gesture = std::get<SingleFingerGesture>(gesture->gesture);
        return single_finger_gesture.status;
    }
    else if (gesture->finger_count == FingerCount::Two)
    {
        const auto& two_finger_gesture = std::get<TwoFingerGesture>(gesture->gesture);
        return two_finger_gesture.status;
    }

    assert(false && "Unhandled case");
    return GestureStatus::Ended;
}

void on_touch_start(GestureSystem* system, const platform::Event& event)
{
    for (auto gesture_id : system->gestures)
    {
        auto gesture = system->gesture_pool.get_object(gesture_id);

        if (get_status(gesture) == GestureStatus::Ended) continue;

        if (gesture->finger_count == FingerCount::One)
        {
            // Convert to two-finger gesture

            // End the single finger
            auto& single_finger_gesture = std::get<SingleFingerGesture>(gesture->gesture);
            single_finger_gesture.status = GestureStatus::Ended;
            gesture->end_time_ms = hrz::now_frame_ms();

            system->events.push_back({Event::Kind::End, single_finger_gesture});

            int first_touch_id = gesture->touch_ids[0];

            // Create a new two-finger gesture
            auto new_gesture_id = system->gesture_pool.alloc();
            auto new_gesture = system->gesture_pool.get_object(new_gesture_id);

            new_gesture->finger_count = FingerCount::Two;
            new_gesture->touch_ids[0] = first_touch_id;
            new_gesture->touch_ids[1] = event.touch.id;

            TwoFingerGesture two_finger_gesture;
            two_finger_gesture.id = new_gesture_id;
            two_finger_gesture.type = TwoFingerGesture::Type::Unqualified;
            two_finger_gesture.initial_positions[0] = single_finger_gesture.position;
            two_finger_gesture.initial_positions[1] = {event.touch.x, event.touch.y};
            two_finger_gesture.positions[0] = two_finger_gesture.initial_positions[0];
            two_finger_gesture.positions[1] = two_finger_gesture.initial_positions[1];
            two_finger_gesture.status = GestureStatus::Ongoing;
            new_gesture->gesture = two_finger_gesture;

            new_gesture->start_time_ms = hrz::now_frame_ms();

            system->gestures.insert(new_gesture_id);

            system->events.push_back({Event::Kind::New, two_finger_gesture, 1});

            return;
        }
    }

    // Create a new one-finger gesture
    auto gesture_id = system->gesture_pool.alloc();
    auto gesture = system->gesture_pool.get_object(gesture_id);

    gesture->finger_count = FingerCount::One;
    gesture->touch_ids[0] = event.touch.id;
    gesture->touch_ids[1] = 0;

    SingleFingerGesture single_finger_gesture;
    single_finger_gesture.id = gesture_id;
    single_finger_gesture.type = SingleFingerGesture::Type::Unqualified;
    single_finger_gesture.eligible_for_tap = true;
    single_finger_gesture.initial_position = {event.touch.x, event.touch.y};
    single_finger_gesture.position = single_finger_gesture.initial_position;
    single_finger_gesture.status = GestureStatus::Ongoing;
    gesture->gesture = single_finger_gesture;

    gesture->start_time_ms = hrz::now_frame_ms();

    system->gestures.insert(gesture_id);

    system->events.push_back({Event::Kind::New, single_finger_gesture, 0});
}

void on_touch_end(GestureSystem* system, const platform::Event& event)
{
    for (auto gesture_id : system->gestures)
    {
        auto gesture = system->gesture_pool.get_object(gesture_id);

        if (get_status(gesture) == GestureStatus::Ended) continue;

        if (gesture->finger_count == FingerCount::One)
        {
            if (gesture->touch_ids[0] == event.touch.id)
            {
                // End gesture
                auto& single_finger_gesture = std::get<SingleFingerGesture>(gesture->gesture);
                single_finger_gesture.status = GestureStatus::Ended;
                gesture->end_time_ms = hrz::now_frame_ms();

                // Browsers emit click events alongside pointer events. This is not programatically
                // preventable. We rely almost uniquely on pointer events to avoid having duplicates
                // between pointer and mouse events. Though, we still use traditional mouse events
                // to detect double-clicks so that we don't have to do it on our own. Browsers have
                // the advantage of being able to take the user's settings into account, so they are
                // better at this task.
                //     -qdebroise, 2022-10-10

                // Qualify
                if (single_finger_gesture.type == SingleFingerGesture::Type::Unqualified)
                {
                    if (single_finger_gesture.eligible_for_tap
                        && single_finger_gesture.displacement_distance() < MAX_DISTANCE_FOR_TAP
                        && gesture->end_time_ms - gesture->start_time_ms < MAX_DURATION_FOR_TAP)
                    {
                        single_finger_gesture.type = SingleFingerGesture::Type::Tap;

                        // Try to qualify as double-tap
                        for (auto previous_gesture_id : system->gestures)
                        {
                            if (previous_gesture_id == gesture_id) continue;

                            auto previous_gesture =
                                system->gesture_pool.get_object(previous_gesture_id);

                            if (get_status(previous_gesture) != GestureStatus::Ended) continue;

                            if (previous_gesture->finger_count == FingerCount::One)
                            {
                                auto& previous_single_finger_gesture =
                                    std::get<SingleFingerGesture>(previous_gesture->gesture);

                                if (previous_single_finger_gesture.type
                                    != SingleFingerGesture::Type::Tap)
                                {
                                    continue;
                                }

#ifndef HRZ_EMSCRIPTEN
                                auto delay = gesture->end_time_ms - previous_gesture->end_time_ms;
                                auto distance = lm::length(
                                    previous_single_finger_gesture.initial_position
                                    - single_finger_gesture.initial_position);
                                if (delay > 0 && delay < MAX_DURATION_FOR_DOUBLE_TAP
                                    && distance < MAX_DISTANCE_FOR_DOUBLE_TAP)
                                {
                                    single_finger_gesture.type =
                                        SingleFingerGesture::Type::DoubleTap;
                                }
#endif
                            }
                        }

                        system->events.push_back(
                            {Event::Kind::Qualification, single_finger_gesture});
                    }
                }

                system->events.push_back({Event::Kind::End, single_finger_gesture});

                return;
            }
        }
        else if (gesture->finger_count == FingerCount::Two)
        {
            if (gesture->touch_ids[0] == event.touch.id || gesture->touch_ids[1] == event.touch.id)
            {
                // End gesture, but create a new one-finger gesture for the remaining finger
                auto& two_finger_gesture = std::get<TwoFingerGesture>(gesture->gesture);
                two_finger_gesture.status = GestureStatus::Ended;
                gesture->end_time_ms = hrz::now_frame_ms();

                system->events.push_back({Event::Kind::End, two_finger_gesture});

                // Create a new one-finger gesture
                size_t remaining_touch_id_index = gesture->touch_ids[0] == event.touch.id ? 1 : 0;
                int remaining_touch_id = gesture->touch_ids[remaining_touch_id_index];

                auto new_gesture_id = system->gesture_pool.alloc();
                auto new_gesture = system->gesture_pool.get_object(new_gesture_id);

                new_gesture->finger_count = FingerCount::One;
                new_gesture->touch_ids[0] = remaining_touch_id;
                new_gesture->touch_ids[1] = 0;

                SingleFingerGesture single_finger_gesture;
                single_finger_gesture.id = new_gesture_id;
                single_finger_gesture.type = SingleFingerGesture::Type::Unqualified;
                single_finger_gesture.eligible_for_tap = false;
                single_finger_gesture.initial_position =
                    two_finger_gesture.positions[remaining_touch_id_index];
                single_finger_gesture.position = single_finger_gesture.initial_position;
                single_finger_gesture.status = GestureStatus::Ongoing;
                new_gesture->gesture = single_finger_gesture;

                new_gesture->start_time_ms = hrz::now_frame_ms();

                system->gestures.insert(new_gesture_id);

                system->events.push_back({Event::Kind::New, single_finger_gesture, 2});

                return;
            }
        }
    }
}

void on_touch_move(GestureSystem* system, const platform::Event& event)
{
    for (auto gesture_id : system->gestures)
    {
        auto gesture = system->gesture_pool.get_object(gesture_id);

        if (get_status(gesture) == GestureStatus::Ended) continue;

        if (gesture->finger_count == FingerCount::One)
        {
            if (gesture->touch_ids[0] == event.touch.id)
            {
                // Update position
                auto& single_finger_gesture = std::get<SingleFingerGesture>(gesture->gesture);
                single_finger_gesture.position = {event.touch.x, event.touch.y};

                // Qualify
                if (single_finger_gesture.type == gestures::SingleFingerGesture::Type::Unqualified)
                {
                    if (single_finger_gesture.displacement_distance()
                        >= DISTANCE_FOR_SINGLE_FINGER_DRAG)
                    {
                        single_finger_gesture.type = gestures::SingleFingerGesture::Type::Drag;

                        system->events.push_back(
                            {Event::Kind::Qualification, single_finger_gesture});
                    }
                }

                system->events.push_back({Event::Kind::Move, single_finger_gesture});

                return;
            }
        }
        else if (gesture->finger_count == FingerCount::Two)
        {
            if (gesture->touch_ids[0] == event.touch.id || gesture->touch_ids[1] == event.touch.id)
            {
                // Update position
                size_t touch_id_index = gesture->touch_ids[0] == event.touch.id ? 0 : 1;

                auto& two_finger_gesture = std::get<TwoFingerGesture>(gesture->gesture);
                two_finger_gesture.positions[touch_id_index] = {event.touch.x, event.touch.y};

                // Qualify
                if (two_finger_gesture.type == gestures::TwoFingerGesture::Type::Unqualified)
                {
                    if (std::abs(angular_difference(
                            two_finger_gesture.orientation(),
                            two_finger_gesture.initial_orientation()))
                        >= ANGLE_FOR_ROTATION)
                    {
                        two_finger_gesture.type = gestures::TwoFingerGesture::Type::PinchRotate;

                        system->events.push_back({Event::Kind::Qualification, two_finger_gesture});
                    }
                    else if (
                        two_finger_gesture.displacement_distance() >= DISTANCE_FOR_TWO_FINGER_DRAG)
                    {
                        two_finger_gesture.type = gestures::TwoFingerGesture::Type::Drag;

                        system->events.push_back({Event::Kind::Qualification, two_finger_gesture});
                    }
                    else
                    {
                        float spread_ratio = two_finger_gesture.spread_ratio();
                        if (spread_ratio <= SPREAD_SHRINK_RATIO_FOR_PINCH
                            || spread_ratio >= SPREAD_GROW_RATIO_FOR_PINCH)
                        {
                            two_finger_gesture.type = gestures::TwoFingerGesture::Type::PinchRotate;

                            system->events.push_back(
                                {Event::Kind::Qualification, two_finger_gesture});
                        }
                    }
                }

                system->events.push_back({Event::Kind::Move, two_finger_gesture});

                return;
            }
        }
    }
}
} // namespace

bool handle_platform_event(GestureSystem* system, const platform::Event& event)
{
    assert(system);

    switch (event.kind)
    {
        case platform::Event::Kind::TouchStart: on_touch_start(system, event); break;
        case platform::Event::Kind::TouchEnd:
        case platform::Event::Kind::TouchCancel:
        {
            on_touch_move(system, event); // Update last touch position
            on_touch_end(system, event);
            break;
        }
        case platform::Event::Kind::TouchMove: on_touch_move(system, event); break;
        default: return false;
    }

    return true;
}

std::optional<SingleFingerGesture> get_single_finger_gesture(
    const GestureSystem* system,
    GestureId gesture_id)
{
    assert(system);

    const auto gesture = system->gesture_pool.get_object(gesture_id);

    if (gesture != nullptr && gesture->finger_count == FingerCount::One)
    {
        return {std::get<SingleFingerGesture>(gesture->gesture)};
    }

    return std::nullopt;
}

std::optional<TwoFingerGesture> get_two_finger_gesture(
    const GestureSystem* system,
    GestureId gesture_id)
{
    assert(system);

    const auto gesture = system->gesture_pool.get_object(gesture_id);

    if (gesture != nullptr && gesture->finger_count == FingerCount::Two)
    {
        return {std::get<TwoFingerGesture>(gesture->gesture)};
    }

    return std::nullopt;
}

void remove_ended_gestures(GestureSystem* system)
{
    assert(system);

    auto now = hrz::now_frame_ms();

    std::erase_if(
        system->gestures,
        [&](GestureId gesture_id)
        {
            const auto gesture = system->gesture_pool.get_object(gesture_id);
            if (get_status(gesture) == GestureStatus::Ended
                && now - gesture->end_time_ms >= ENDED_GESTURE_TTL)
            {
                system->gesture_pool.release(gesture_id);
                return true;
            }
            return false;
        });
}

bool dequeue_event(GestureSystem* system, Event& event)
{
    assert(system);

    if (!system->events.empty())
    {
        event = system->events.front();
        system->events.pop_front();
        return true;
    }
    else
    {
        return false;
    }
}
} // namespace gestures
} // namespace hrz
