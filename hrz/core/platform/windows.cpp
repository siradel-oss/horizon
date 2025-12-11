#include "hrz/core/platform/platform.h"
#include "hrz/core/platform/windows_context.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/format.h"

#include <assert.h>
#include <windows.h>
#include <windowsx.h>

#include <deque>

extern "C"
{
#include <microui/microui.h>
}

namespace
{

hrz::platform::Event::Key vk_to_key[256];
HCURSOR arrow_cursor;

} // namespace

namespace hrz
{

static platform::Event::MouseButton convert_button(int b)
{
    switch (b)
    {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK: return platform::Event::MouseButton::Left;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK: return platform::Event::MouseButton::Middle;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK: return platform::Event::MouseButton::Right;
        default: return platform::Event::MouseButton::Left;
    }
}

static LRESULT CALLBACK MyWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    PlatformContext* ctx = (PlatformContext*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_CLOSE:
        {
            ctx->events.push_back(platform::Event::make_window_closed());
            return 0;
        }
        case WM_MOUSELEAVE:
        {
            if (ctx->ignore_user_interaction_events) break;

            ctx->events.push_back({platform::Event::Kind::MouseLeave});
            return 0;
        }
        case WM_KEYDOWN:
        {
            if (ctx->ignore_user_interaction_events) break;

            bool repeated = (HIWORD(lParam) & KF_REPEAT) == KF_REPEAT;
            if (!repeated && wParam < 256)
            {
                platform::Event::Key key = vk_to_key[wParam];
                if (key != platform::Event::Key::_None)
                {
                    ctx->events.push_back(platform::Event::make_key_down(key));
                    ctx->down_keys.insert(key);
                    return 0;
                }
            }
            break;
        }
        case WM_KEYUP:
        {
            if (ctx->ignore_user_interaction_events) break;

            if (wParam < 256)
            {
                platform::Event::Key key = vk_to_key[wParam];
                if (key != platform::Event::Key::_None)
                {
                    ctx->events.push_back(platform::Event::make_key_up(key));
                    ctx->down_keys.erase(key);
                    return 0;
                }
            }
            break;
        }
        case WM_CHAR:
        {
            if (ctx->ignore_user_interaction_events) break;

            if (wParam >= 32 && wParam < 127)
            {
                bool repeated = (HIWORD(lParam) & KF_REPEAT) == KF_REPEAT;
                char c = (char)wParam;
                ctx->events.push_back(platform::Event::make_character(c, repeated));
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE:
        {
            if (ctx->ignore_user_interaction_events) break;

            TRACKMOUSEEVENT tme = {sizeof(tme)};
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            int dx = x - ctx->mouse_x;
            int dy = y - ctx->mouse_y;
            ctx->mouse_x = x;
            ctx->mouse_y = y;
            ctx->events.push_back(platform::Event::make_mouse_move(x, y, dx, dy));
            return 0;
        }
        case WM_MOUSEWHEEL:
        {
            if (ctx->ignore_user_interaction_events) break;

            // Mouse wheel coords are relative to screen, not client (window) area!
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            float fy = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            ctx->events.push_back(platform::Event::make_mouse_wheel(pt.x, pt.y, fy));
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        {
            if (ctx->ignore_user_interaction_events) break;

            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            auto button = convert_button(uMsg);
            ctx->down_mouse_buttons.insert(button);
            ctx->events.push_back(platform::Event::make_mouse_button_down(button, x, y));

            // Get mouse events even if the pointer goes outside the window.
            // (Useful for drag operations.)
            SetCapture(hwnd);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        {
            if (ctx->ignore_user_interaction_events) break;

            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            auto button = convert_button(uMsg);
            ctx->down_mouse_buttons.erase(button);
            ctx->events.push_back(platform::Event::make_mouse_button_up(button, x, y));
            // Testing that we have no buttons still pressed prevents sending a "CAPTURECHANGED"
            // event when we release only 1 button of a multi-button drag, which would forcibly
            // "unpress" all buttons.
            if (ctx->down_mouse_buttons.empty())
            {
                ReleaseCapture();
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        {
            if (ctx->ignore_user_interaction_events) break;

            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            auto button = convert_button(uMsg);
            ctx->events.push_back(platform::Event::make_mouse_button_double_click(button, x, y));
            return 0;
        }
        case WM_SIZE:
        {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            ctx->new_width = w;
            ctx->new_height = h;
            return 0;
        }
        case WM_SETCURSOR:
        {
            if (LOWORD(lParam) == HTCLIENT)
            {
                SetCursor(arrow_cursor);
                return 1;
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
        case WM_CAPTURECHANGED:
        {
            // Drag operations can be interrupted by some events other than the mouse
            // buttons being released, such as with alt+tab.
            // See
            // https://www.codeproject.com/Tips/127813/Using-SetCapture-and-ReleaseCapture-correctly-usua
            if ((HWND)lParam != hwnd)
            {
                ctx->release_all_mouse_down();
                return 0;
            }
        }
        case WM_KILLFOCUS:
        {
            if ((HWND)lParam != hwnd)
            {
                ctx->release_all_key_down();
                return 0;
            }
        }
        default: break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

namespace platform
{
PlatformContext* initialize(
    bool disable_events_capture,
    void* wsi_instance,
    void* wsi_window,
    const char* canvas_selector,
    float device_pixel_ratio_override)
{
    memset((void*)vk_to_key, 0, sizeof(vk_to_key));

    vk_to_key[VK_ESCAPE] = Event::Key::Escape;
    vk_to_key[VK_F1] = Event::Key::F1;
    vk_to_key[VK_F2] = Event::Key::F2;
    vk_to_key[VK_F3] = Event::Key::F3;
    vk_to_key[VK_F4] = Event::Key::F4;
    vk_to_key[VK_F5] = Event::Key::F5;
    vk_to_key[VK_F6] = Event::Key::F6;
    vk_to_key[VK_F7] = Event::Key::F7;
    vk_to_key[VK_F8] = Event::Key::F8;
    vk_to_key[VK_F9] = Event::Key::F9;
    vk_to_key[VK_F10] = Event::Key::F10;
    vk_to_key[VK_F11] = Event::Key::F11;
    vk_to_key[VK_F12] = Event::Key::F12;
    vk_to_key['1'] = Event::Key::N1;
    vk_to_key['2'] = Event::Key::N2;
    vk_to_key['3'] = Event::Key::N3;
    vk_to_key['4'] = Event::Key::N4;
    vk_to_key['5'] = Event::Key::N5;
    vk_to_key['6'] = Event::Key::N6;
    vk_to_key['7'] = Event::Key::N7;
    vk_to_key['8'] = Event::Key::N8;
    vk_to_key['9'] = Event::Key::N9;
    vk_to_key['0'] = Event::Key::N0;
    vk_to_key['A'] = Event::Key::A;
    vk_to_key['B'] = Event::Key::B;
    vk_to_key['C'] = Event::Key::C;
    vk_to_key['D'] = Event::Key::D;
    vk_to_key['E'] = Event::Key::E;
    vk_to_key['F'] = Event::Key::F;
    vk_to_key['G'] = Event::Key::G;
    vk_to_key['H'] = Event::Key::H;
    vk_to_key['I'] = Event::Key::I;
    vk_to_key['J'] = Event::Key::J;
    vk_to_key['K'] = Event::Key::K;
    vk_to_key['L'] = Event::Key::L;
    vk_to_key['M'] = Event::Key::M;
    vk_to_key['N'] = Event::Key::N;
    vk_to_key['O'] = Event::Key::O;
    vk_to_key['P'] = Event::Key::P;
    vk_to_key['Q'] = Event::Key::Q;
    vk_to_key['R'] = Event::Key::R;
    vk_to_key['S'] = Event::Key::S;
    vk_to_key['T'] = Event::Key::T;
    vk_to_key['U'] = Event::Key::U;
    vk_to_key['V'] = Event::Key::V;
    vk_to_key['W'] = Event::Key::W;
    vk_to_key['X'] = Event::Key::X;
    vk_to_key['Y'] = Event::Key::Y;
    vk_to_key['Z'] = Event::Key::Z;
    vk_to_key[VK_TAB] = Event::Key::Tab;
    vk_to_key[VK_CAPITAL] = Event::Key::CapsLock;
    vk_to_key[VK_LSHIFT] = Event::Key::Shift;
    vk_to_key[VK_LCONTROL] = Event::Key::Ctrl;
    vk_to_key[VK_LMENU] = Event::Key::Alt;
    vk_to_key[VK_SHIFT] = Event::Key::Shift;
    vk_to_key[VK_CONTROL] = Event::Key::Ctrl;
    vk_to_key[VK_MENU] = Event::Key::Alt;
    vk_to_key[VK_SPACE] = Event::Key::Space;
    vk_to_key[VK_BACK] = Event::Key::Backspace;
    vk_to_key[VK_RETURN] = Event::Key::Enter;
    vk_to_key[VK_RSHIFT] = Event::Key::Shift;
    vk_to_key[VK_RCONTROL] = Event::Key::Ctrl;
    vk_to_key[VK_RMENU] = Event::Key::Alt;
    vk_to_key[VK_UP] = Event::Key::Up;
    vk_to_key[VK_DOWN] = Event::Key::Down;
    vk_to_key[VK_LEFT] = Event::Key::Left;
    vk_to_key[VK_RIGHT] = Event::Key::Right;
    vk_to_key[VK_INSERT] = Event::Key::Insert;
    vk_to_key[VK_DELETE] = Event::Key::Delete;
    vk_to_key[VK_HOME] = Event::Key::Home;
    vk_to_key[VK_END] = Event::Key::End;
    vk_to_key[VK_PRIOR] = Event::Key::PageUp;
    vk_to_key[VK_NEXT] = Event::Key::PageDown;

    PlatformContext* ctx = new PlatformContext;
    ctx->enable_events_capture = !disable_events_capture;
    ctx->ignore_user_interaction_events = false;
    ctx->hinstance = (HINSTANCE)wsi_instance;
    ctx->hwnd = (HWND)wsi_window;

    if (ctx->enable_events_capture)
    {
        SetWindowLongPtr(ctx->hwnd, GWLP_USERDATA, (LONG_PTR)ctx);
        SetWindowLongPtr(ctx->hwnd, GWLP_WNDPROC, (LONG_PTR)&MyWindowProc);
        SetClassLongPtr(ctx->hwnd, GCL_STYLE, (LONG_PTR)CS_DBLCLKS);
    }
    arrow_cursor = LoadCursor(nullptr, IDC_ARROW);

    auto size = get_current_canvas_size(ctx);
    if (size)
    {
        ctx->old_width = ctx->new_width = size->x;
        ctx->old_height = ctx->new_height = size->y;
    }

    return ctx;
}

std::optional<lm::uvec2> get_current_canvas_size(const PlatformContext* ctx)
{
    RECT rect;
    GetClientRect(ctx->hwnd, &rect);
    return lm::uvec2(rect.right - rect.left, rect.bottom - rect.top);
}

void advance_events(PlatformContext* ctx)
{
    assert(ctx);

    if (ctx->enable_events_capture)
    {
        MSG msg;
        while (PeekMessage(&msg, ctx->hwnd, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // We don't want too many window resized events
        if (ctx->old_width != ctx->new_width || ctx->old_height != ctx->new_height)
        {
            ctx->events.push_back(Event::make_window_resized(ctx->new_width, ctx->new_height));
            ctx->old_width = ctx->new_width;
            ctx->old_height = ctx->new_height;
        }
    }
}

void cleanup(PlatformContext* ctx)
{
    assert(ctx);

    cleanup_gl(ctx);

    if (ctx->enable_events_capture)
    {
        SetWindowLongPtr(ctx->hwnd, GWLP_USERDATA, (LONG_PTR) nullptr);
        SetWindowLongPtr(ctx->hwnd, GWLP_WNDPROC, (LONG_PTR)DefWindowProc);
    }

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
    ctx->release_all_down();
}

void copy_to_clipboard(PlatformContext* ctx, std::string_view text)
{
    if (OpenClipboard(ctx->hwnd))
    {
        HANDLE hmem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (hmem)
        {
            LPTSTR dst = (LPTSTR)GlobalLock(hmem);
            if (dst)
            {
                for (size_t i = 0; i < text.size(); ++i)
                {
                    *dst++ = text[i];
                }
                *dst = 0;
                GlobalUnlock(hmem);
            }

            EmptyClipboard();
            SetClipboardData(CF_TEXT, hmem);
        }

        CloseClipboard();
    }
}

void add_key_to_capture(PlatformContext*, hrz::platform::Event::Key) {}

void add_key_bypassing_focus(PlatformContext*, hrz::platform::Event::Key) {}

float get_current_device_pixel_ratio(const PlatformContext* ctx)
{
    return 1.0f;
}

void viewport_dev_ui(PlatformContext* ctx, mu_Context* ui)
{
    fmt::memory_buffer buffer;

    {
        static int layout[] = {150, -1};
        mu_layout_row(ui, 2, layout, 0);
    }

    mu_text(ui, "Window size");
    mu_text(ui, format_to_buffer(buffer, "{} x {}", ctx->new_width, ctx->new_height));
}

} // namespace platform
} // namespace hrz
