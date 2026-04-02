#include "hrz/core/platform/linux_x11_context.h"
#include "hrz/core/platform/platform.h"
#include "hrz/fnd/format.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/time.h"

#include <unistd.h>

#include <cassert>
#include <deque>
#include <limits>
#include <optional>

extern "C"
{
#include <microui/microui.h>
}

namespace
{

constexpr float DoubleClickDelayMs = 400;           // ms
constexpr float DoubleClickMaxDistanceSquared = 16; // pixels

} // namespace

namespace hrz::platform
{
namespace
{

static Event::Key keycode_to_key[256];

bool initialize_touch_input(PlatformContext* ctx)
{
    // Based on https://github.com/esjeon/xinput2-touch/blob/master/event.c

    // Check that the XInput extension is available.
    {
        int xi_opcode;
        int ev;
        int err;
        if (!XQueryExtension(ctx->display, "XInputExtension", &xi_opcode, &ev, &err))
        {
            HRZ_LOG_INFO("XInput extension not available");
            return false;
        }

        ctx->xi_opcode = {xi_opcode};
    }

    // Check that XInput 2.2 (or greater) is available.
    {
        int major = 2;
        int minor = 2;
        Status res = XIQueryVersion(ctx->display, &major, &minor);

        if (res != Success || major * 1000 + minor < 2002)
        {
            HRZ_LOG_INFO("XInput version 2.2 not supported");
            return false;
        }
    }

    int touch_device_id;

    // Select touch device
    {
        bool touch_device_found = false;
        int num_devices;
        XIDeviceInfo* di = XIQueryDevice(ctx->display, XIAllDevices, &num_devices);
        for (int i = 0; i < num_devices; ++i)
        {
            XIDeviceInfo* dev = &di[i];
            for (int j = 0; j < dev->num_classes; ++j)
            {
                XITouchClassInfo* device_class = (XITouchClassInfo*)(dev->classes[j]);
                if (device_class->type != XITouchClass)
                {
                    touch_device_id = dev->deviceid;
                    touch_device_found = true;
                    goto STOP_SEARCH_DEVICE;
                }
            }
        }
    STOP_SEARCH_DEVICE:
        XIFreeDeviceInfo(di);

        if (!touch_device_found)
        {
            return false;
        }
    }

    // Select events to listen to
    {
        constexpr size_t mask_length = XIMaskLen(XI_TouchEnd);
        unsigned char mask_data[mask_length];
        std::memset(mask_data, 0, mask_length);
        XIEventMask mask = {touch_device_id, mask_length, mask_data};
        XISetMask(mask.mask, XI_TouchBegin);
        XISetMask(mask.mask, XI_TouchUpdate);
        XISetMask(mask.mask, XI_TouchEnd);

        XISelectEvents(ctx->display, ctx->window, &mask, 1);
    }

    return true;
}

} // namespace

PlatformContext* initialize(
    bool disable_events_capture,
    void* wsi_instance,
    void* wsi_window,
    const char* canvas_selector,
    float device_pixel_ratio_override)
{
    PlatformContext* ctx = new PlatformContext;
    ctx->display = (Display*)wsi_instance;
    ctx->window = (Window)wsi_window;
    ctx->gl_ctx = nullptr;
    ctx->clipboard_window = None;
    ctx->enable_events_capture = !disable_events_capture;
    ctx->ignore_user_interaction_events = false;

    if (ctx->enable_events_capture)
    {
        ctx->dblclk_info.start_time_ms = hrz::now_ms();
        ctx->dblclk_info.start_mouse_x = std::numeric_limits<int>::lowest();
        ctx->dblclk_info.start_mouse_y = std::numeric_limits<int>::lowest();
        ctx->dblclk_info.button = (Event::MouseButton)-1;

        memset(ctx->key_mask, 0, sizeof(uint32_t) * 8);
        memset(keycode_to_key, 0, sizeof(Event::Key) * 256);

        ctx->close_window_atom = XInternAtom(ctx->display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(ctx->display, ctx->window, &ctx->close_window_atom, 1);

        XSelectInput(
            ctx->display, ctx->window,
            KeyPressMask | KeyReleaseMask | StructureNotifyMask | PointerMotionMask
                | ButtonPressMask | ButtonReleaseMask | LeaveWindowMask);

        if (initialize_touch_input(ctx))
        {
            HRZ_LOG_INFO("Touch input enabled");
        }
    }

    XFlush(ctx->display);

    keycode_to_key[9] = Event::Key::Escape;
    keycode_to_key[67] = Event::Key::F1;
    keycode_to_key[68] = Event::Key::F2;
    keycode_to_key[69] = Event::Key::F3;
    keycode_to_key[70] = Event::Key::F4;
    keycode_to_key[71] = Event::Key::F5;
    keycode_to_key[72] = Event::Key::F6;
    keycode_to_key[73] = Event::Key::F7;
    keycode_to_key[74] = Event::Key::F8;
    keycode_to_key[75] = Event::Key::F9;
    keycode_to_key[76] = Event::Key::F10;
    keycode_to_key[95] = Event::Key::F11;
    keycode_to_key[96] = Event::Key::F12;
    keycode_to_key[19] = Event::Key::N0;
    keycode_to_key[10] = Event::Key::N1;
    keycode_to_key[11] = Event::Key::N2;
    keycode_to_key[12] = Event::Key::N3;
    keycode_to_key[13] = Event::Key::N4;
    keycode_to_key[14] = Event::Key::N5;
    keycode_to_key[15] = Event::Key::N6;
    keycode_to_key[16] = Event::Key::N7;
    keycode_to_key[17] = Event::Key::N8;
    keycode_to_key[18] = Event::Key::N9;
    keycode_to_key[23] = Event::Key::Tab;
    keycode_to_key[66] = Event::Key::CapsLock;
    keycode_to_key[50] = Event::Key::Shift;
    keycode_to_key[62] = Event::Key::Shift;
    keycode_to_key[37] = Event::Key::Ctrl;
    keycode_to_key[105] = Event::Key::Ctrl;
    keycode_to_key[64] = Event::Key::Alt;
    keycode_to_key[108] = Event::Key::Alt;
    keycode_to_key[65] = Event::Key::Space;
    keycode_to_key[22] = Event::Key::Backspace;
    keycode_to_key[36] = Event::Key::Enter;
    keycode_to_key[111] = Event::Key::Up;
    keycode_to_key[116] = Event::Key::Down;
    keycode_to_key[113] = Event::Key::Left;
    keycode_to_key[114] = Event::Key::Right;
    keycode_to_key[118] = Event::Key::Insert;
    keycode_to_key[119] = Event::Key::Delete;
    keycode_to_key[110] = Event::Key::Home;
    keycode_to_key[115] = Event::Key::End;
    keycode_to_key[112] = Event::Key::PageUp;
    keycode_to_key[117] = Event::Key::PageDown;

    ctx->clipboard_atom = XInternAtom(ctx->display, "CLIPBOARD", False);
    ctx->targets_atom = XInternAtom(ctx->display, "TARGETS", False);
    ctx->utf8_string_atom = XInternAtom(ctx->display, "UTF8_STRING", False);
    ctx->text_plain_utf8_atom = XInternAtom(ctx->display, "text/plain;charset=utf-8", False);
    ctx->text_plain_atom = XInternAtom(ctx->display, "text/plain", False);
    ctx->text_atom = XInternAtom(ctx->display, "TEXT", False);
    ctx->hrz_clipboard_atom = XInternAtom(ctx->display, "HRZ_CLIPBOARD", False);

    auto size = get_current_canvas_size(ctx);
    if (size)
    {
        ctx->width = size->x;
        ctx->height = size->y;
    }

    return ctx;
}

std::optional<lm::uvec2> get_current_canvas_size(const PlatformContext* ctx)
{
    XWindowAttributes watt;
    XGetWindowAttributes(ctx->display, ctx->window, &watt);
    return lm::uvec2(watt.width, watt.height);
}

static Event::Key translate_key(PlatformContext* ctx, XKeyEvent* ev, char& char_value)
{
    char_value = 0;

    char buffer[32];
    KeySym keysym;
    int size = XLookupString(ev, buffer, 32, &keysym, nullptr);

    if (size == 1)
    {
        if (buffer[0] >= 32 && buffer[0] < 127)
        {
            char_value = buffer[0];
        }
    }

    KeySym key = XkbKeycodeToKeysym(ctx->display, ev->keycode, 0, 0);
    if (key >= XK_a && key <= XK_z)
    {
        return (Event::Key)((int)Event::Key::A + (key - XK_a));
    }

    if ((int)ev->keycode < 256)
    {
        return keycode_to_key[ev->keycode];
    }

    return Event::Key::_None;
}

void advance_events(PlatformContext* ctx)
{
    assert(ctx);

    if (!ctx->enable_events_capture) return;

    const double now = hrz::now_ms();

    while (XPending(ctx->display))
    {
        XEvent ev;
        XGenericEventCookie* cookie = &ev.xcookie;
        XNextEvent(ctx->display, &ev);

        if (ctx->xi_opcode.has_value() && XGetEventData(ctx->display, cookie)) // Extended event
        {
            if (ctx->ignore_user_interaction_events) continue;

            // Check if this belongs to XInput
            if (cookie->type == GenericEvent && cookie->extension == ctx->xi_opcode.value())
            {
                XIDeviceEvent* devev = (XIDeviceEvent*)cookie->data;

                // `devev->detail` holds the touch id for touch events.

                switch (devev->evtype)
                {
                    case XI_TouchBegin:
                        ctx->events.push_back(
                            platform::Event::make_touch_start(
                                devev->detail, devev->event_x, devev->event_y));
                        break;
                    case XI_TouchUpdate:
                        ctx->events.push_back(
                            platform::Event::make_touch_move(
                                devev->detail, devev->event_x, devev->event_y));
                        break;
                    case XI_TouchEnd:
                        ctx->events.push_back(
                            platform::Event::make_touch_end(
                                devev->detail, devev->event_x, devev->event_y));
                        break;
                }
            }
        }
        else // Normal event
        {
            switch (ev.type)
            {
                case ClientMessage:
                {
                    if (((Atom)ev.xclient.data.l[0]) == ctx->close_window_atom)
                    {
                        ctx->events.push_back(platform::Event::make_window_closed());
                    }
                    break;
                }
                case ConfigureNotify:
                {
                    if (ctx->width != ev.xconfigure.width || ctx->height != ev.xconfigure.height)
                    {
                        ctx->width = ev.xconfigure.width;
                        ctx->height = ev.xconfigure.height;
                        ctx->events.push_back(
                            platform::Event::make_window_resized(ctx->width, ctx->height));
                    }
                    break;
                }
                case KeyPress:
                {
                    if (ctx->ignore_user_interaction_events) break;

                    char char_value = 0;
                    bool repeated = ctx->is_key_pressed((uint8_t)ev.xkey.keycode);
                    auto key = translate_key(ctx, &ev.xkey, char_value);

                    if (char_value != 0)
                    {
                        ctx->events.push_back(
                            platform::Event::make_character(char_value, repeated));
                    }

                    if (!repeated && key != Event::Key::_None)
                    {
                        ctx->events.push_back(platform::Event::make_key_down(key));
                        ctx->down_keys.insert(key);
                    }

                    ctx->press_key((uint8_t)ev.xkey.keycode);
                    break;
                }
                case KeyRelease:
                {
                    if (ctx->ignore_user_interaction_events) break;

                    if (XPending(ctx->display))
                    {
                        XEvent next;
                        XPeekEvent(ctx->display, &next);
                        if (next.type == KeyPress && next.xkey.keycode == ev.xkey.keycode
                            && next.xkey.time == ev.xkey.time)
                        {
                            // This is a repeat. Ignore!
                            break;
                        }
                    }

                    char char_value = 0;
                    auto key = translate_key(ctx, &ev.xkey, char_value);

                    if (key != Event::Key::_None)
                    {
                        ctx->events.push_back(platform::Event::make_key_up(key));
                        ctx->down_keys.erase(key);
                    }

                    ctx->release_key((uint8_t)ev.xkey.keycode);
                    break;
                }
                case MotionNotify:
                {
                    if (ctx->ignore_user_interaction_events) break;

                    int x = ev.xmotion.x;
                    int y = ev.xmotion.y;
                    int dx = x - ctx->mouse_x;
                    int dy = y - ctx->mouse_y;
                    ctx->events.push_back(platform::Event::make_mouse_move(x, y, dx, dy));
                    ctx->mouse_x = x;
                    ctx->mouse_y = y;
                    break;
                }
                case ButtonPress:
                {
                    if (ctx->ignore_user_interaction_events) break;

                    if (ev.xbutton.button >= 4 && ev.xbutton.button <= 5)
                    {
                        float value;
                        switch (ev.xbutton.button)
                        {
                            case 4: value = 1; break;
                            case 5: value = -1; break;
                            default: value = 0; break;
                        }
                        if (value != 0)
                        {
                            ctx->events.push_back(
                                platform::Event::make_mouse_wheel(
                                    ctx->mouse_x, ctx->mouse_y, value));
                        }
                    }
                    else if (ev.xbutton.button >= 1 && ev.xbutton.button <= 3)
                    {
                        if (ctx->ignore_user_interaction_events) break;

                        int x = ev.xbutton.x;
                        int y = ev.xbutton.y;
                        Event::MouseButton button;
                        switch (ev.xbutton.button)
                        {
                            case 1: button = Event::MouseButton::Left; break;
                            case 2: button = Event::MouseButton::Middle; break;
                            case 3: button = Event::MouseButton::Right; break;
                            default: button = Event::MouseButton::Left; break;
                        }

                        double elapsed_time_ms = now - ctx->dblclk_info.start_time_ms;
                        float distance_squared = (ctx->dblclk_info.start_mouse_x - x)
                                * (ctx->dblclk_info.start_mouse_x - x)
                            + (ctx->dblclk_info.start_mouse_y - y)
                                * (ctx->dblclk_info.start_mouse_y - y);
                        if (button == ctx->dblclk_info.button
                            && elapsed_time_ms < DoubleClickDelayMs
                            && distance_squared < DoubleClickMaxDistanceSquared)
                        {
                            ctx->events.push_back(
                                platform::Event::make_mouse_button_double_click(button, x, y));
                            ctx->dblclk_info.button = (Event::MouseButton)-1;
                        }
                        else
                        {
                            ctx->events.push_back(
                                platform::Event::make_mouse_button_down(button, x, y));
                            ctx->down_mouse_buttons.insert(button);
                            ctx->dblclk_info.button = button;
                        }

                        ctx->dblclk_info.start_time_ms = now;
                        ctx->dblclk_info.start_mouse_x = x;
                        ctx->dblclk_info.start_mouse_y = y;
                    }
                    break;
                }
                case ButtonRelease:
                {
                    if (ctx->ignore_user_interaction_events) break;

                    if (ev.xbutton.button >= 1 && ev.xbutton.button <= 3)
                    {
                        int x = ev.xbutton.x;
                        int y = ev.xbutton.y;
                        Event::MouseButton button;
                        switch (ev.xbutton.button)
                        {
                            case 1: button = Event::MouseButton::Left; break;
                            case 2: button = Event::MouseButton::Middle; break;
                            case 3: button = Event::MouseButton::Right; break;
                            default: button = Event::MouseButton::Left; break;
                        }
                        ctx->events.push_back(platform::Event::make_mouse_button_up(button, x, y));
                        ctx->down_mouse_buttons.insert(button);
                    }
                    break;
                }
                case LeaveNotify:
                {
                    if (ctx->ignore_user_interaction_events) break;

                    // NotifyGrab is emitted when refocussing the window with the mouse.
                    // We don't want a mouse leave event when it happens.
                    if (ev.xcrossing.mode != NotifyGrab)
                    {
                        ctx->events.push_back({platform::Event::Kind::MouseLeave});
                    }
                    break;
                }
                case SelectionRequest:
                {
                    if (ctx->ignore_user_interaction_events) break;

                    // https://github.com/libsdl-org/SDL/blob/120c76c84bbce4c1bfed4e9eb74e10678bd83120/src/video/x11/SDL_x11events.c#L602
                    const XSelectionRequestEvent& req = ev.xselectionrequest;

                    XEvent sevent;
                    memset(&sevent, 0, sizeof(XEvent));
                    sevent.xany.type = SelectionNotify;
                    sevent.xselection.selection = req.selection;
                    sevent.xselection.target = None;
                    sevent.xselection.property = None;
                    sevent.xselection.requestor = req.requestor;
                    sevent.xselection.time = req.time;

                    Atom supported_formats[PlatformContext::ClipboardMimeTypeCount + 1];

                    if (req.target == ctx->targets_atom)
                    {
                        supported_formats[0] = ctx->targets_atom;
                        for (size_t i = 0; i < PlatformContext::ClipboardMimeTypeCount; ++i)
                        {
                            supported_formats[i + 1] = ctx->get_clipboard_mime_type_atom(
                                (PlatformContext::ClipboardMimeType)i);
                        }

                        XChangeProperty(
                            ctx->display, req.requestor, req.property, XA_ATOM, 32, PropModeReplace,
                            (unsigned char*)supported_formats,
                            PlatformContext::ClipboardMimeTypeCount + 1);

                        sevent.xselection.property = req.property;
                        sevent.xselection.target = ctx->targets_atom;
                    }
                    else
                    {
                        for (size_t i = 0; i < PlatformContext::ClipboardMimeTypeCount; ++i)
                        {
                            if (ctx->get_clipboard_mime_type_atom(
                                    (PlatformContext::ClipboardMimeType)i)
                                != req.target)
                            {
                                continue;
                            }

                            int seln_format;
                            unsigned long nbytes;
                            unsigned long overflow;
                            unsigned char* seln_data;

                            if (XGetWindowProperty(
                                    ctx->display, DefaultRootWindow(ctx->display),
                                    ctx->hrz_clipboard_atom, 0, INT_MAX / 4, False,
                                    ctx->utf8_string_atom, &sevent.xselection.target, &seln_format,
                                    &nbytes, &overflow, &seln_data)
                                == Success)
                            {
                                if (seln_format != None)
                                {
                                    XChangeProperty(
                                        ctx->display, req.requestor, req.property,
                                        sevent.xselection.target, seln_format, PropModeReplace,
                                        seln_data, nbytes);
                                    sevent.xselection.property = req.property;
                                    XFree(seln_data);
                                    break;
                                }
                                else
                                {
                                    XFree(seln_data);
                                }
                            }
                        }
                    }

                    XSendEvent(ctx->display, req.requestor, False, 0, &sevent);
                    XSync(ctx->display, False);
                    break;
                }
                default: break;
            }
        }
    }
}

void cleanup(PlatformContext* ctx)
{
    assert(ctx);

    cleanup_gl(ctx);

    delete ctx;
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

    for (auto button : ctx->down_mouse_buttons)
    {
        ctx->events.push_back(
            platform::Event::make_mouse_button_up(button, ctx->mouse_x, ctx->mouse_y));
    }
    ctx->down_mouse_buttons.clear();

    for (auto key : ctx->down_keys)
    {
        ctx->events.push_back(platform::Event::make_key_up(key));
    }
    ctx->down_keys.clear();
}

// https://github.com/libsdl-org/SDL/blob/7967c97618fba7326028005e72ead740fa6e6589/src/video/x11/SDL_x11clipboard.c#L123
void copy_to_clipboard(PlatformContext* ctx, std::string_view text)
{
    // https://github.com/libsdl-org/SDL/blob/7967c97618fba7326028005e72ead740fa6e6589/src/video/x11/SDL_x11clipboard.c#L38
    if (ctx->clipboard_window == None)
    {
        Window parent = RootWindow(ctx->display, DefaultScreen(ctx->display));
        XSetWindowAttributes xattr;
        ctx->clipboard_window = XCreateWindow(
            ctx->display, parent, -10, -10, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, 0,
            &xattr);
        XFlush(ctx->display);
    }

    XChangeProperty(
        ctx->display, DefaultRootWindow(ctx->display), ctx->hrz_clipboard_atom,
        ctx->utf8_string_atom, 8, PropModeReplace, (const unsigned char*)text.data(), text.size());

    XSetSelectionOwner(ctx->display, ctx->clipboard_atom, ctx->clipboard_window, CurrentTime);
}

void add_key_to_capture(PlatformContext*, hrz::platform::Event::Key) {}

void add_key_bypassing_focus(PlatformContext*, hrz::platform::Event::Key) {}

float get_current_device_pixel_ratio(const PlatformContext* ctx)
{
    return 1.0F;
}

void viewport_dev_ui(PlatformContext* ctx, mu_Context* ui)
{
    fmt::memory_buffer buffer;

    {
        static int layout[] = {150, -1};
        mu_layout_row(ui, 2, layout, 0);
    }

    mu_text(ui, "Window size");
    mu_text(ui, format_to_buffer(buffer, "{} x {}", ctx->width, ctx->height));
}

} // namespace hrz::platform
