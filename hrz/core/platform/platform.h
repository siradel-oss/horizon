#pragma once

#include "hrz/core/platform/events.h"
#include "hrz/protocol/viewer/init_status.pb.h"

#include <lin_maths.h>

#include <optional>
#include <string_view>

namespace hrz
{
struct PlatformContext;

namespace platform
{
PlatformContext* initialize(
    bool disable_events_capture,
    void* wsi_instance,
    void* wsi_window,
    const char* canvas_selector,
    float device_pixel_ratio_override);

hrz_proto::ViewerInitStatus initialize_gl_ctx(PlatformContext*);
void make_gl_ctx_current(const PlatformContext*);

using GlLoadFn = void* (*)(const char*);
GlLoadFn get_gl_load_fn();

void swap_window(const PlatformContext*);
void advance_events(PlatformContext*);
bool dequeue_event(PlatformContext*, Event*);
void set_user_interactions_enabled(PlatformContext*, bool enabled);
void cleanup(PlatformContext*);
void copy_to_clipboard(PlatformContext*, std::string_view);

// Adds a key to the list of keys captured by the platform layer (when
// capturing is enabled). This is useful for platforms that have a complex
// events propagation model, so right now only the web platform. By default key
// events are not captured. This functions makes sure that some key events are
// never propagated upward, but only when Horizon has focus.
void add_key_to_capture(PlatformContext*, hrz::platform::Event::Key);

// Adds a key to watch events for when the window or canvas is out of focus.
// Other keys (and most other events) are not reported when out of focus.
// This is only implemented for the web platform.
void add_key_bypassing_focus(PlatformContext*, hrz::platform::Event::Key);

std::optional<lm::uvec2> get_current_canvas_size(const PlatformContext*);

float get_current_device_pixel_ratio(const PlatformContext*);

} // namespace platform
} // namespace hrz
