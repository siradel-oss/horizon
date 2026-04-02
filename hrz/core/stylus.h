#pragma once

#include "hrz/core/platform/events.h"

namespace hrz
{

struct StylusSystem;

namespace stylus
{

/**
 * Creates a stylus system.
 *
 * This system is used to map stylus events to mouse events. In theory, stylus events can handle pen
 * tilting, hovering and things like that and should therefore be separate from mouse events. But,
 * right now this serves very little purposes. It is simpler in our current case to have a simple
 * system that isolates the translation of stylus events to mouse events. This system can do some
 * internal processing to improve the user experience and it can be build upon later on when a more
 * sophisicated stylus handling will be required.
 *      qdebroise - 13/10/22
 */
StylusSystem* create_system();

/**
 * Destroys a stylus system.
 */
void destroy_system(StylusSystem*);

/**
 * Sends platform events to the stylus system to interpret them.
 */
bool handle_platform_event(StylusSystem*, const platform::Event&);

/**
 * Removes the oldest element from the queue and returns it. The event returned _is_ a mouse event.
 */
bool dequeue_event(StylusSystem*, platform::Event&);

} // namespace stylus
} // namespace hrz
