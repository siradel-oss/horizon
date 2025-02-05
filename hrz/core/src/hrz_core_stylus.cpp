#include "hrz_core_stylus.h"

#include <lin_maths.h>

#include <cassert>
#include <deque>

namespace
{
static constexpr float DISTANCE_FOR_DRAG = 10; // pixels
} // anonymous namespace

namespace hrz
{
struct StylusSystem
{
    struct Stylus
    {
        lm::ivec2 initial_position;
        lm::ivec2 last_position;
        lm::ivec2 position;
        bool qualified_as_drag = false;
        bool is_down = false;

        inline float displacement_distance() const
        {
            return lm::length(position - initial_position);
        }

        inline lm::ivec2 relative_distance() const { return position - last_position; }
    };

    std::deque<platform::Event> events;
    Stylus stylus;
};

namespace
{
void on_stylus_move(StylusSystem* ss, const platform::Event& event)
{
    auto& stylus = ss->stylus;
    stylus.position = {event.stylus.x, event.stylus.y};

    lm::ivec2 pos = stylus.position;
    lm::ivec2 rel = stylus.relative_distance();

    if (!stylus.is_down)
    {
        ss->events.push_back(platform::Event::make_mouse_move(pos.x, pos.y, rel.x, rel.y));
    }
    else if (
        stylus.is_down
        && (stylus.qualified_as_drag || stylus.displacement_distance() > DISTANCE_FOR_DRAG))
    {
        ss->events.push_back(platform::Event::make_mouse_move(pos.x, pos.y, rel.x, rel.y));
        stylus.qualified_as_drag = true;
    }

    stylus.last_position = pos;
}

void on_stylus_down(StylusSystem* ss, const platform::Event& event)
{
    auto& stylus = ss->stylus;

    stylus.initial_position = {event.stylus.x, event.stylus.y};
    stylus.is_down = true;
    ss->events.push_back(platform::Event::make_mouse_button_down(
        platform::Event::MouseButton::Left, event.stylus.x, event.stylus.y));
}

void on_stylus_up(StylusSystem* ss, const platform::Event& event)
{
    auto& stylus = ss->stylus;

    stylus.is_down = false;
    stylus.qualified_as_drag = false;

    ss->events.push_back(platform::Event::make_mouse_button_up(
        platform::Event::MouseButton::Left, event.stylus.x, event.stylus.y));
}

void on_stylus_leave(StylusSystem* ss, const platform::Event& event)
{
    ss->events.push_back(platform::Event{platform::Event::Kind::MouseLeave});
}
} // anonymous namespace

namespace stylus
{
StylusSystem* create_system()
{
    return new StylusSystem();
}

void destroy_system(StylusSystem* ss)
{
    assert(ss);
    delete ss;
}

bool handle_platform_event(StylusSystem* ss, const platform::Event& event)
{
    assert(ss);

    switch (event.kind)
    {
        case platform::Event::Kind::StylusMove: on_stylus_move(ss, event); break;
        case platform::Event::Kind::StylusDown: on_stylus_down(ss, event); break;
        case platform::Event::Kind::StylusUp: on_stylus_up(ss, event); break;
        case platform::Event::Kind::StylusLeave: on_stylus_leave(ss, event); break;
        default: return false;
    }
    return true;
}

bool dequeue_event(StylusSystem* ss, platform::Event& event)
{
    assert(ss);

    if (!ss->events.empty())
    {
        event = ss->events.front();
        ss->events.pop_front();
        return true;
    }
    return false;
}
} // namespace stylus
} // namespace hrz
