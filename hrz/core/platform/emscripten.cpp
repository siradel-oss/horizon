
#include "hrz/core/js/lib.h"
#include "hrz/core/platform/platform.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/format.h"
#include "hrz/fnd/log.h"

#include <assert.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include <deque>
#include <utility>

extern "C"
{
#include <microui/microui.h>
}

namespace
{
enum class PointerType : int
{
    Unknown = 0,
    Touch = 1,
    Pen = 2,
    Mouse = 3,
};

struct TouchPosition
{
    int x;
    int y;
};

// https://www.w3.org/TR/pointerevents/#button-states
static hrz::platform::Event::MouseButton translate_single_button(unsigned short b)
{
    switch (b)
    {
        case 0: return hrz::platform::Event::MouseButton::Left;
        case 1: return hrz::platform::Event::MouseButton::Middle;
        case 2: return hrz::platform::Event::MouseButton::Right;
        default: return hrz::platform::Event::MouseButton::Left;
    }
}

struct MouseButtonSetTraits
{
    static constexpr uint8_t MaxCount = 3;
    using Type = hrz::platform::Event::MouseButton;

    // The people who decided the bits in "buttons" shouldn't be at the same index as their id in
    // "button" are very nice.

    // https://www.w3.org/TR/pointerevents/#the-buttons-property
    static hrz::platform::Event::MouseButton translate_bit(unsigned short b)
    {
        switch (b)
        {
            case 0: return hrz::platform::Event::MouseButton::Left;
            case 1: return hrz::platform::Event::MouseButton::Right;
            case 2: return hrz::platform::Event::MouseButton::Middle;
            default: return hrz::platform::Event::MouseButton::Left;
        }
    }
};

struct ModKeySetTraits
{
    static constexpr uint8_t MaxCount = 3;
    using Type = hrz::platform::Event::Key;

    static hrz::platform::Event::Key translate_bit(unsigned short b)
    {
        switch (b)
        {
            case 0: return hrz::platform::Event::Key::Ctrl;
            case 1: return hrz::platform::Event::Key::Shift;
            case 2: return hrz::platform::Event::Key::Alt;
            default: return hrz::platform::Event::Key::_None;
        }
    }
};

uint8_t key_to_mod_key_mask(hrz::platform::Event::Key key)
{
    switch (key)
    {
        case hrz::platform::Event::Key::Ctrl: return 1;
        case hrz::platform::Event::Key::Shift: return 2;
        case hrz::platform::Event::Key::Alt: return 4;
        default: return 0;
    }
}

template<typename Traits>
struct ControlIterator
{
    using Self = ControlIterator<Traits>;

    uint8_t mask{};
    uint8_t it{};

    std::pair<typename Traits::Type, bool> operator*() const
    {
        return std::make_pair(Traits::translate_bit((unsigned short)it), (mask & (1 << it)) != 0);
    }

    Self& operator++()
    {
        it += 1;
        return *this;
    }

    Self operator++(int) const { return {mask, (uint8_t)(it + 1)}; }

    constexpr bool operator==(const Self& other) const { return it == other.it; }

    constexpr bool operator!=(const Self& other) const { return it != other.it; }
};

template<typename Traits>
struct ControlSet
{
    using Iterator = ControlIterator<Traits>;

    uint8_t mask{};

    Iterator begin() const { return {mask, 0}; }

    Iterator end() const { return {mask, Traits::MaxCount}; }

    static ControlSet up(ControlSet before, ControlSet after)
    {
        return ControlSet{(uint8_t)(before.mask & (~after.mask))};
    }

    static ControlSet down(ControlSet before, ControlSet after)
    {
        return ControlSet{(uint8_t)((~before.mask) & after.mask)};
    }
};

using MouseButtonSet = ControlSet<MouseButtonSetTraits>;
using ModKeySet = ControlSet<ModKeySetTraits>;

} // anonymous namespace

namespace hrz
{
struct PlatformContext
{
    std::string canvas;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE gl_ctx;
    std::deque<platform::Event> events;

    lm::vec2 css_size{400.0f, 300.0f};
    lm::vec2 device_size{400.0f, 300.0f};
    lm::ivec2 render_size{400, 300};
    int override_device_pixel_ratio = 0;
    float overriden_device_pixel_ratio = 1.0f;
    float actual_device_pixel_ratio = 1.0f;

    constexpr float device_pixel_ratio() const
    {
        return override_device_pixel_ratio ? overriden_device_pixel_ratio
                                           : actual_device_pixel_ratio;
    }

    bool has_focus = false;
    int mouse_x = 0;
    int mouse_y = 0;
    MouseButtonSet down_mouse_buttons;

    int stylus_x = 0;
    int stylus_y = 0;
    bool down_stylus = false;

    hrz::flat_hash_map<int, TouchPosition> touch_pointer_id_to_position;
    hrz::flat_hash_set<platform::Event::Key> down_keys;
    ModKeySet down_mod_keys;

    bool enable_events_capture;
    bool ignore_user_interaction_events;
    hrz::flat_hash_set<platform::Event::Key> keys_to_capture;
    hrz::flat_hash_set<platform::Event::Key> keys_bypassing_focus;

    void event_to_render_xy(float* x, float* y)
    {
        float ratio = device_pixel_ratio();
        *x *= ratio;
        *y *= ratio;
    }

    void apply_mod_key_mask(uint8_t mask)
    {
        ModKeySet new_set = {mask};
        ModKeySet down = ModKeySet::down(down_mod_keys, new_set);
        ModKeySet up = ModKeySet::up(down_mod_keys, new_set);

        for (const auto& k : down)
        {
            if (k.second)
            {
                handle_key_down(k.first, 0, false);
            }
        }

        for (const auto& k : up)
        {
            if (k.second)
            {
                handle_key_up(k.first);
            }
        }
    }

    bool handle_key_down(platform::Event::Key key, char c, bool repeat)
    {
        if (!has_focus && keys_bypassing_focus.count(key) == 0)
        {
            return false;
        }

        if (!repeat)
        {
            if (key != platform::Event::Key::_None)
            {
                events.push_back(platform::Event::make_key_down(key));
                down_keys.insert(key);

                uint8_t mod = key_to_mod_key_mask(key);
                down_mod_keys.mask |= mod;
            }
        }

        if (c != 0)
        {
            events.push_back(platform::Event::make_character(c, repeat));
        }

        if (keys_to_capture.count(key) > 0)
        {
            // We shouldn't capture key events when we don't have focus.
            return has_focus;
        }
        else
        {
            return false;
        }
    }

    bool handle_key_up(platform::Event::Key key)
    {
        if (!has_focus && keys_bypassing_focus.count(key) == 0)
        {
            return false;
        }

        if (key != platform::Event::Key::_None)
        {
            events.push_back(platform::Event::make_key_up(key));
            down_keys.erase(key);

            uint8_t mod = key_to_mod_key_mask(key);
            down_mod_keys.mask &= ~mod;
        }

        if (keys_to_capture.count(key) > 0)
        {
            // We shouldn't capture key events when we don't have focus.
            return has_focus;
        }
        else
        {
            return false;
        }
    }

    void release_all_keys()
    {
        for (auto key : down_keys)
        {
            events.push_back(platform::Event::make_key_up(key));
        }
        down_keys.clear();

        down_mod_keys.mask = 0;
    }
};

namespace platform
{
static char translate_char(const char key[32])
{
    if (key[1] == 0 && key[0] >= 32 && key[0] < 127)
    {
        return key[0];
    }
    return 0;
}

static Event::Key translate_key(const char key[32], const char code[32])
{
    if (key[1] == 0)
    {
        char c = key[0];

        if (c >= 'A' && c <= 'Z')
        {
            int num = c - 'A';
            return (Event::Key)((int)Event::Key::A + num);
        }

        if (c >= 'a' && c <= 'z')
        {
            int num = c - 'a';
            return (Event::Key)((int)Event::Key::A + num);
        }
    }

    // We switch on the first character to reduce the number of string comparisons
    // we have to do...
    switch (code[0])
    {
        case 'A':
            if (strncmp(code, "Alt", 3) == 0)
            {
                if (code[3] == 0 || strcmp(code + 3, "Left") == 0 || strcmp(code + 3, "Right") == 0)
                {
                    return Event::Key::Alt;
                }
            }
            else if (strncmp(code, "Arrow", 5) == 0)
            {
                if (strcmp(code + 5, "Left") == 0)
                    return Event::Key::Left;
                else if (strcmp(code + 5, "Right") == 0)
                    return Event::Key::Right;
                else if (strcmp(code + 5, "Up") == 0)
                    return Event::Key::Up;
                else if (strcmp(code + 5, "Down") == 0)
                    return Event::Key::Down;
            }
            break;
        case 'B':
            if (strcmp(code, "Backspace") == 0) return Event::Key::Backspace;
            break;
        case 'C':
            if (strcmp(code, "CapsLock") == 0)
                return Event::Key::CapsLock;
            else if (strncmp(code, "Control", 7) == 0)
            {
                if (code[7] == 0 || strcmp(code + 7, "Left") == 0 || strcmp(code + 7, "Right") == 0)
                {
                    return Event::Key::Ctrl;
                }
            }
            break;
        case 'D':
            if (strncmp(code, "Digit", 5) == 0 && code[5] >= '0' && code[5] <= '9' && code[6] == 0)
            {
                int num = code[5] - '0';
                return (Event::Key)((int)Event::Key::N0 + num);
            }
            else if (strcmp(code, "Delete") == 0)
                return Event::Key::Delete;
            break;
        case 'E':
            if (strcmp(code, "Escape") == 0)
                return Event::Key::Escape;
            else if (strcmp(code, "Enter") == 0)
                return Event::Key::Enter;
            else if (strcmp(code, "End") == 0)
                return Event::Key::End;
            break;
        case 'F':
            if (code[1] == '1')
            {
                if (code[2] == 0)
                {
                    return Event::Key::F1;
                }
                else if (code[3] == 0 && code[2] >= '0' && code[2] <= '2')
                {
                    int num = code[2] - '0';
                    return (Event::Key)((int)Event::Key::F10 + num);
                }
            }
            else if (code[2] == 0 && code[1] >= '1' && code[1] <= '9')
            {
                int num = code[1] - '1';
                return (Event::Key)((int)Event::Key::F1 + num);
            }
            break;
        case 'H':
            if (strcmp(code, "Home") == 0) return Event::Key::Home;
            break;
        case 'I':
            if (strcmp(code, "Insert") == 0) return Event::Key::Insert;
            break;
        case 'P':
            if (strcmp(code, "PageUp") == 0)
                return Event::Key::PageUp;
            else if (strcmp(code, "PageDown") == 0)
                return Event::Key::PageDown;
            break;
        case 'S':
            if (strncmp(code, "Shift", 5) == 0)
            {
                if (code[5] == 0 || strcmp(code + 5, "Left") == 0 || strcmp(code + 5, "Right") == 0)
                {
                    return Event::Key::Shift;
                }
            }
            else if (strcmp(code, "Space") == 0)
                return Event::Key::Space;
        case 'T':
            if (strcmp(code, "Tab") == 0) return Event::Key::Tab;
            break;
    }

    return Event::Key::_None;
}

static EM_BOOL canvas_mouse_callback(
    int eventType,
    const EmscriptenMouseEvent* mouseEvent,
    void* userData)
{
    PlatformContext* ctx = (PlatformContext*)userData;

    if (ctx->ignore_user_interaction_events) return false;

    switch (eventType)
    {
        // We want to capture some mouse events that occur inside the canvas to prevent browsers
        // to use them for other purposes.
        case EMSCRIPTEN_EVENT_MOUSEDOWN: return true;
        case EMSCRIPTEN_EVENT_DBLCLICK:
        {
            float x = (float)mouseEvent->targetX;
            float y = (float)mouseEvent->targetY;
            ctx->event_to_render_xy(&x, &y);

            auto button = translate_single_button(mouseEvent->button);
            ctx->events.push_back(platform::Event::make_mouse_button_double_click(button, x, y));
            return true;
        }
        default: return false;
    }

    return false;
}

static EM_BOOL wheel_callback(int eventType, const EmscriptenWheelEvent* wheelEvent, void* userData)
{
    PlatformContext* ctx = (PlatformContext*)userData;

    if (ctx->ignore_user_interaction_events) return false;

    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_WHEEL:
        {
            float scroll = -wheelEvent->deltaY;
            switch (wheelEvent->deltaMode)
            {
                case DOM_DELTA_LINE: scroll /= 10.0f; break;
                case DOM_DELTA_PAGE: scroll /= (float)ctx->css_size.y; break;
                default: break;
            }
            scroll /= 100.0f;
            if (scroll != 0)
            {
                ctx->events.push_back(
                    platform::Event::make_mouse_wheel(ctx->mouse_x, ctx->mouse_y, scroll));
                return true;
            }
            break;
        }
        default: return false;
    }

    return false;
}

static EM_BOOL focusin_callback(
    int eventType,
    const EmscriptenFocusEvent* focusEvent,
    void* userData)
{
    PlatformContext* ctx = (PlatformContext*)userData;

    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_FOCUSIN:
        {
            ctx->has_focus = true;
            break;
        }
        default: return false;
    }

    return false;
}

static EM_BOOL focusout_callback(
    int eventType,
    const EmscriptenFocusEvent* focusEvent,
    void* userData)
{
    PlatformContext* ctx = (PlatformContext*)userData;

    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_FOCUSOUT:
        {
            ctx->has_focus = false;
            break;
        }
        default: return false;
    }

    return false;
}

static EM_BOOL key_callback(int eventType, const EmscriptenKeyboardEvent* keyEvent, void* userData)
{
    PlatformContext* ctx = (PlatformContext*)userData;

    if (ctx->ignore_user_interaction_events) return false;

    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_KEYDOWN:
        {
            auto key = translate_key(keyEvent->key, keyEvent->code);
            char c = translate_char(keyEvent->key);
            return ctx->handle_key_down(key, c, keyEvent->repeat);
        }
        case EMSCRIPTEN_EVENT_KEYUP:
        {
            auto key = translate_key(keyEvent->key, keyEvent->code);
            return ctx->handle_key_up(key);
        }
        default: return false;
    }

    return false;
}

static void update_mouse_buttons_state(
    PlatformContext* ctx,
    float x,
    float y,
    MouseButtonSet new_buttons)
{
    auto up = MouseButtonSet::up(ctx->down_mouse_buttons, new_buttons);
    auto down = MouseButtonSet::down(ctx->down_mouse_buttons, new_buttons);

    for (auto b : up)
    {
        if (b.second)
        {
            ctx->events.push_back(platform::Event::make_mouse_button_up(b.first, x, y));
        }
    }

    for (auto b : down)
    {
        if (b.second)
        {
            ctx->events.push_back(platform::Event::make_mouse_button_down(b.first, x, y));
        }
    }

    ctx->down_mouse_buttons = new_buttons;
}

static void pointer_down_callback(
    int pointerId,
    int pointerType,
    float x,
    float y,
    int buttons,
    int mod_keys,
    void* userData)
{
    PlatformContext* ctx = (PlatformContext*)userData;

    if (ctx->ignore_user_interaction_events) return;

    ctx->apply_mod_key_mask(mod_keys);
    ctx->event_to_render_xy(&x, &y);

    if ((PointerType)pointerType == PointerType::Touch)
    {
        ctx->events.push_back(platform::Event::make_touch_start(pointerId, x, y));
        ctx->touch_pointer_id_to_position.insert({pointerId, {(int)x, (int)y}});
    }
    else if ((PointerType)pointerType == PointerType::Pen)
    {
        ctx->events.push_back(platform::Event::make_stylus_down(x, y));
        ctx->down_stylus = true;
    }
    else if ((PointerType)pointerType == PointerType::Mouse)
    {
        update_mouse_buttons_state(ctx, x, y, MouseButtonSet{(uint8_t)buttons});
    }
}

static void pointer_up_callback(
    int pointerId,
    int pointerType,
    float x,
    float y,
    int buttons,
    int mod_keys,
    void* userData)
{
    PlatformContext* ctx = (PlatformContext*)userData;

    if (ctx->ignore_user_interaction_events) return;

    ctx->apply_mod_key_mask(mod_keys);
    ctx->event_to_render_xy(&x, &y);

    if ((PointerType)pointerType == PointerType::Touch)
    {
        ctx->events.push_back(platform::Event::make_touch_end(pointerId, x, y));
        ctx->touch_pointer_id_to_position.erase(pointerId);
    }
    else if ((PointerType)pointerType == PointerType::Pen)
    {
        ctx->events.push_back(platform::Event::make_stylus_up(x, y));
        ctx->down_stylus = false;
    }
    else if ((PointerType)pointerType == PointerType::Mouse)
    {
        update_mouse_buttons_state(ctx, x, y, MouseButtonSet{(uint8_t)buttons});
    }
}

static void pointer_move_callback(
    int pointerId,
    int pointerType,
    float x,
    float y,
    int buttons,
    int mod_keys,
    void* userData)
{
    PlatformContext* ctx = (PlatformContext*)userData;

    if (ctx->ignore_user_interaction_events) return;

    ctx->apply_mod_key_mask(mod_keys);
    ctx->event_to_render_xy(&x, &y);

    if ((PointerType)pointerType == PointerType::Touch)
    {
        ctx->events.push_back(platform::Event::make_touch_move(pointerId, x, y));
        auto it = ctx->touch_pointer_id_to_position.find(pointerId);
        if (it != ctx->touch_pointer_id_to_position.end())
        {
            it->second = {(int)x, (int)y};
        }
    }
    else if ((PointerType)pointerType == PointerType::Pen)
    {
        ctx->events.push_back(platform::Event::make_stylus_move(x, y));
        ctx->stylus_x = x;
        ctx->stylus_y = y;
    }
    else if ((PointerType)pointerType == PointerType::Mouse)
    {
        int ix = (int)x;
        int iy = (int)y;
        int dx = ix - ctx->mouse_x;
        int dy = iy - ctx->mouse_y;
        ctx->events.push_back(platform::Event::make_mouse_move(ix, iy, dx, dy));
        ctx->mouse_x = ix;
        ctx->mouse_y = iy;

        // We want to detect newly pressed buttons only if we have already pressed some buttons.
        // We don't want a drag that starts outside the canvas to be interpreted as a button down
        // once the mouse enters the canvas. The first button press will be detected by the pointer
        // down callback only if it's on the canvas.
        if (ctx->down_mouse_buttons.mask != 0)
        {
            update_mouse_buttons_state(ctx, x, y, MouseButtonSet{(uint8_t)buttons});
        }
    }
}

static void pointer_cancel_callback(
    int pointerId,
    int pointerType,
    float x,
    float y,
    int buttons,
    int mod_keys,
    void* userData)
{
    PlatformContext* ctx = (PlatformContext*)userData;

    if (ctx->ignore_user_interaction_events) return;

    ctx->apply_mod_key_mask(mod_keys);
    ctx->event_to_render_xy(&x, &y);

    if ((PointerType)pointerType == PointerType::Touch)
    {
        ctx->events.push_back(platform::Event::make_touch_cancel(pointerId, x, y));
    }
}

static void pointer_leave_callback(
    int pointerId,
    int pointerType,
    float x,
    float y,
    int buttons,
    int mod_keys,
    void* userData)
{
    PlatformContext* ctx = (PlatformContext*)userData;

    if (ctx->ignore_user_interaction_events) return;

    ctx->apply_mod_key_mask(mod_keys);
    ctx->event_to_render_xy(&x, &y);

    if ((PointerType)pointerType == PointerType::Pen)
    {
        ctx->events.push_back({platform::Event::Kind::StylusLeave});
    }
    else if ((PointerType)pointerType == PointerType::Mouse)
    {
        ctx->events.push_back({platform::Event::Kind::MouseLeave});
    }
}

void resize_observer_callback(
    float css_width,
    float css_height,
    float device_width,
    float device_height,
    void* user_data)
{
    PlatformContext* ctx = (PlatformContext*)user_data;

    ctx->css_size = {css_width, css_height};
    ctx->device_size = {device_width, device_height};
}

PlatformContext* initialize(
    bool disable_events_capture,
    void* wsi_instance,
    void* wsi_window,
    const char* canvas_selector,
    float device_pixel_ratio_override)
{
    PlatformContext* ctx = new PlatformContext();
    ctx->canvas = canvas_selector;
    ctx->enable_events_capture = !disable_events_capture;
    ctx->ignore_user_interaction_events = false;

    if (!hrz_js_register_canvas(ctx->canvas.c_str()))
    {
        HRZ_LOG_ERROR("Could not register canvas with the selector \"{}\"", ctx->canvas);
    }

    ctx->actual_device_pixel_ratio = hrz_js_get_device_pixel_ratio();

    if (device_pixel_ratio_override > 0.0f)
    {
        ctx->override_device_pixel_ratio = 1;
        ctx->overriden_device_pixel_ratio = device_pixel_ratio_override;
    }

    // Get an estimate of the initial size of the canvas. This will be refined by the ResizeObserver
    // later.
    {
        double dwidth = 0, dheight = 0;
        auto get_size_res = emscripten_get_element_css_size(ctx->canvas.c_str(), &dwidth, &dheight);
        if (get_size_res == EMSCRIPTEN_RESULT_SUCCESS)
        {
            float device_pixel_ratio = ctx->device_pixel_ratio();
            ctx->css_size = {(float)dwidth, (float)dheight};
            ctx->device_size = ctx->css_size * device_pixel_ratio;
            ctx->render_size = lm::ivec2(ctx->device_size);
            if (ctx->render_size.x > 0 && ctx->render_size.y > 0)
            {
                ctx->events.push_back(
                    platform::Event::make_window_resized(ctx->render_size.x, ctx->render_size.y));
                emscripten_set_canvas_element_size(
                    ctx->canvas.c_str(), ctx->render_size.x, ctx->render_size.y);
            }
        }
    }

    if (!hrz_js_install_resize_observer(resize_observer_callback, ctx))
    {
        HRZ_LOG_ERROR("Could not install ResizeObserver");
    }

    if (ctx->enable_events_capture)
    {
        emscripten_set_keydown_callback(
            EMSCRIPTEN_EVENT_TARGET_WINDOW, (void*)ctx, false, key_callback);
        emscripten_set_keyup_callback(
            EMSCRIPTEN_EVENT_TARGET_WINDOW, (void*)ctx, false, key_callback);
        emscripten_set_mousedown_callback(
            ctx->canvas.c_str(), (void*)ctx, false, canvas_mouse_callback);
        emscripten_set_dblclick_callback(
            ctx->canvas.c_str(), (void*)ctx, false, canvas_mouse_callback);
        emscripten_set_wheel_callback(ctx->canvas.c_str(), (void*)ctx, false, wheel_callback);
        emscripten_set_focusin_callback(ctx->canvas.c_str(), (void*)ctx, false, focusin_callback);
        emscripten_set_focusout_callback(ctx->canvas.c_str(), (void*)ctx, false, focusout_callback);

        hrz_js_set_pointer_down_handler(
            ctx->canvas.c_str(), (void*)ctx, true, pointer_down_callback);
        hrz_js_set_pointer_up_handler(ctx->canvas.c_str(), (void*)ctx, true, pointer_up_callback);
        hrz_js_set_pointer_move_handler(
            ctx->canvas.c_str(), (void*)ctx, true, pointer_move_callback);
        hrz_js_set_pointer_cancel_handler(
            ctx->canvas.c_str(), (void*)ctx, true, pointer_cancel_callback);
        hrz_js_set_pointer_leave_handler(
            ctx->canvas.c_str(), (void*)ctx, false, pointer_leave_callback);
    }

    // @Workaround(012-Emscripten-ModuleLeakOnExit)
    hrz_js_install_module_leak_fix();

    return ctx;
}

namespace
{
bool try_enable_webgl_extension(
    PlatformContext* ctx,
    const char* extension_name,
    hrz::log::Severity failure_log_severity)
{
    if (emscripten_webgl_enable_extension(ctx->gl_ctx, extension_name))
    {
        HRZ_LOG_DEBUG("WebGL extension {} enabled", extension_name);
        return true;
    }
    else
    {
        HRZ_LOG(failure_log_severity, "Could not enable WebGL extension {}", extension_name);
        return false;
    }
}
} // namespace

hrz_proto::ViewerInitStatus initialize_gl_ctx(PlatformContext* ctx)
{
    assert(ctx);

    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.alpha = true;
    attributes.premultipliedAlpha = true;
    attributes.depth = false;
    attributes.stencil = false;
    attributes.antialias = false;
    attributes.majorVersion = 2;
    attributes.minorVersion = 0;
    attributes.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;

    ctx->gl_ctx = emscripten_webgl_create_context(ctx->canvas.c_str(), &attributes);
    if (ctx->gl_ctx <= 0)
    {
        HRZ_LOG_ERROR("Failed to create a WebGL 2 context: {}", ctx->gl_ctx);
        return hrz_proto::ViewerInitStatus::RENDERING_API_CONTEXT_CREATION_ERROR;
    }

    if (!try_enable_webgl_extension(ctx, "EXT_color_buffer_float", hrz::log::Severity::Error))
    {
        return hrz_proto::ViewerInitStatus::RENDERING_API_MISSING_FEATURE_ERROR;
    }

    try_enable_webgl_extension(ctx, "WEBGL_debug_renderer_info", hrz::log::Severity::Info);
    try_enable_webgl_extension(ctx, "KHR_parallel_shader_compile", hrz::log::Severity::Info);
    try_enable_webgl_extension(ctx, "WEBGL_provoking_vertex", hrz::log::Severity::Debug);
    try_enable_webgl_extension(ctx, "OES_texture_float_linear", hrz::log::Severity::Debug);
    try_enable_webgl_extension(ctx, "WEBGL_compressed_texture_s3tc", hrz::log::Severity::Debug);
    try_enable_webgl_extension(
        ctx, "WEBGL_compressed_texture_s3tc_srgb", hrz::log::Severity::Debug);
    try_enable_webgl_extension(ctx, "EXT_texture_compression_bptc", hrz::log::Severity::Debug);
    try_enable_webgl_extension(ctx, "WEBGL_compressed_texture_etc1", hrz::log::Severity::Debug);
    try_enable_webgl_extension(ctx, "WEBGL_compressed_texture_etc", hrz::log::Severity::Debug);
    try_enable_webgl_extension(ctx, "WEBGL_compressed_texture_astc", hrz::log::Severity::Debug);
    try_enable_webgl_extension(ctx, "WEBGL_compressed_texture_pvrtc", hrz::log::Severity::Debug);
    try_enable_webgl_extension(
        ctx, "WEBKIT_WEBGL_compressed_texture_pvrtc", hrz::log::Severity::Debug);

    return hrz_proto::ViewerInitStatus::INIT_SUCCESS;
}

GlLoadFn get_gl_load_fn()
{
    return nullptr;
}

void make_gl_ctx_current(const PlatformContext* ctx)
{
    assert(ctx);
    emscripten_webgl_make_context_current(ctx->gl_ctx);
}

void swap_window(const PlatformContext* ctx)
{
    // No-op, handled by main event loop.
}

std::optional<lm::uvec2> get_current_canvas_size(const PlatformContext* ctx)
{
    return lm::uvec2(ctx->render_size);
}

bool is_canvas_valid(PlatformContext* ctx)
{
    double dwidth = 0, dheight = 0;
    auto get_size_res = emscripten_get_element_css_size(ctx->canvas.c_str(), &dwidth, &dheight);
    return get_size_res == EMSCRIPTEN_RESULT_SUCCESS;
}

void advance_events(PlatformContext* ctx)
{
    assert(ctx);

    float new_device_pixel_ratio = hrz_js_get_device_pixel_ratio();

    lm::ivec2 new_render_size = ctx->override_device_pixel_ratio
        ? lm::ivec2(ctx->overriden_device_pixel_ratio * ctx->css_size)
        : lm::ivec2(ctx->device_size);

    if ((new_render_size != ctx->render_size
         || ctx->actual_device_pixel_ratio != new_device_pixel_ratio))
    {
        ctx->actual_device_pixel_ratio = new_device_pixel_ratio;
        ctx->render_size = new_render_size;
        ctx->events.push_back(
            platform::Event::make_window_resized(ctx->render_size.x, ctx->render_size.y));
        emscripten_set_canvas_element_size(
            ctx->canvas.c_str(), ctx->render_size.x, ctx->render_size.y);
    }

    if (!is_canvas_valid(ctx))
    {
        // None of the ways of detecting the loss of the WebGL context work, so
        // here we are...
        ctx->events.push_back(platform::Event::make_window_closed());
        return;
    }
}

void cleanup(PlatformContext* ctx)
{
    assert(ctx);
    hrz_js_uninstall_resize_observer();
    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr);
    emscripten_cancel_main_loop();
    emscripten_webgl_destroy_context(ctx->gl_ctx);
    delete ctx;
    exit(0);
}

bool dequeue_event(PlatformContext* ctx, Event* event)
{
    assert(ctx);
    if (!ctx->events.empty())
    {
        *event = ctx->events.front();
        ctx->events.pop_front();
        return true;
    }
    else
    {
        return false;
    }
}

void set_user_interactions_enabled(PlatformContext* ctx, bool enabled)
{
    ctx->ignore_user_interaction_events = !enabled;

    update_mouse_buttons_state(ctx, ctx->mouse_x, ctx->mouse_y, MouseButtonSet{0});

    ctx->release_all_keys();

    for (auto& pair : ctx->touch_pointer_id_to_position)
    {
        ctx->events.push_back(
            platform::Event::make_touch_end(pair.first, pair.second.x, pair.second.y));
    }
    ctx->touch_pointer_id_to_position.clear();

    if (ctx->down_stylus)
    {
        ctx->events.push_back(platform::Event::make_stylus_up(ctx->stylus_x, ctx->stylus_y));
    }
    ctx->down_stylus = false;
}

void copy_to_clipboard(PlatformContext*, std::string_view text)
{
    hrz_js_copy_string_to_clipboard(text.data());
}

void add_key_to_capture(PlatformContext* ctx, Event::Key key)
{
    ctx->keys_to_capture.insert(key);
}

void add_key_bypassing_focus(PlatformContext* ctx, Event::Key key)
{
    ctx->keys_bypassing_focus.insert(key);
}

void viewport_dev_ui(PlatformContext* ctx, mu_Context* ui)
{
    fmt::memory_buffer buffer;

    {
        static int layout[] = {150, -1};
        mu_layout_row(ui, 2, layout, 0);
    }

    mu_text(ui, "CSS size");
    mu_text(ui, format_to_buffer(buffer, "{:.2f} x {:.2f}", ctx->css_size.x, ctx->css_size.y));

    mu_text(ui, "Device size");
    mu_text(
        ui, format_to_buffer(buffer, "{:.2f} x {:.2f}", ctx->device_size.x, ctx->device_size.y));

    mu_text(ui, "Render size");
    mu_text(ui, format_to_buffer(buffer, "{} x {}", ctx->render_size.x, ctx->render_size.y));

    mu_text(ui, "Platform device pixel ratio");
    mu_text(ui, format_to_buffer(buffer, "{}", ctx->actual_device_pixel_ratio));

    {
        static int layout[] = {-1};
        mu_layout_row(ui, 1, layout, 0);
    }

    mu_checkbox(
        ui,
        format_to_buffer(
            buffer, "Override device pixel ratio ({})", ctx->overriden_device_pixel_ratio),
        &ctx->override_device_pixel_ratio);

    if (ctx->override_device_pixel_ratio)
    {
        static int layout[] = {50, 50, 50, 40};
        mu_layout_row(ui, 4, layout, 0);
        if (mu_button(ui, "0.5")) ctx->overriden_device_pixel_ratio = 0.5f;
        if (mu_button(ui, "0.75")) ctx->overriden_device_pixel_ratio = 0.75f;
        if (mu_button(ui, "0.9")) ctx->overriden_device_pixel_ratio = 0.9f;
        if (mu_button(ui, "1")) ctx->overriden_device_pixel_ratio = 1.0f;
        if (mu_button(ui, "1.1")) ctx->overriden_device_pixel_ratio = 1.1f;
        if (mu_button(ui, "1.5")) ctx->overriden_device_pixel_ratio = 1.5f;
        if (mu_button(ui, "1.618")) ctx->overriden_device_pixel_ratio = 1.618f;
        if (mu_button(ui, "2")) ctx->overriden_device_pixel_ratio = 2.0f;
    }
}

float get_current_device_pixel_ratio(const PlatformContext* ctx)
{
    return ctx->device_pixel_ratio();
}

} // namespace platform
} // namespace hrz
