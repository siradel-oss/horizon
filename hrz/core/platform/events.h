#pragma once

namespace hrz::platform
{

struct Event
{
    // All keys except letters are layout independent.
    // This must match in hrz_input.proto exactly.
    enum class Key
    {
        _None = 0,
        Escape,
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,
        N0,
        N1,
        N2,
        N3,
        N4,
        N5,
        N6,
        N7,
        N8,
        N9,
        A,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,
        Tab,
        CapsLock,
        Shift,
        Ctrl,
        Alt,
        Space,
        Backspace,
        Enter,
        Up,
        Down,
        Left,
        Right,
        Insert,
        Delete,
        Home,
        End,
        PageUp,
        PageDown,
    };

    // This must match in hrz_input.proto exactly.
    enum class MouseButton
    {
        Left,
        Middle,
        Right,
    };

    enum class Kind
    {
        WindowClosed,
        WindowResized,
        Character,
        KeyDown,
        KeyUp,
        MouseMove,
        MouseWheel,
        MouseButtonDown,
        MouseButtonUp,
        MouseButtonDoubleClick,
        MouseLeave,
        TouchStart,
        TouchEnd,
        TouchMove,
        TouchCancel,
        StylusMove,
        StylusDown,
        StylusUp,
        StylusLeave,
    };

    Kind kind;

    union
    {
        struct
        {
            char c;
            bool repeated;
        } character;

        Key key;

        // Mouse coordinates are relative to top-left corner
        struct
        {
            int x;
            int y;
            int dx;
            int dy;
        } mouse_move;

        // Positive = away from user
        struct
        {
            int x;
            int y;
            float wheel;
        } mouse_wheel;

        struct
        {
            MouseButton button;
            int x;
            int y;
        } mouse_button;

        // Touch coordinates are relative to top-left corner
        struct
        {
            int id;
            float x;
            float y;
        } touch;

        struct
        {
            int x;
            int y;
        } stylus;

        struct
        {
            int width;
            int height;
        } window_resized;
    };

    bool is_in_viewport(int xmin, int ymin, int xmax, int ymax) const
    {
        int mx, my;

        switch (kind)
        {
            case Kind::MouseWheel:
                mx = mouse_wheel.x;
                my = mouse_wheel.y;
                break;
            case Kind::MouseButtonDown:
            case Kind::MouseButtonUp:
            case Kind::MouseButtonDoubleClick:
                mx = mouse_button.x;
                my = mouse_button.y;
                break;
            case Kind::MouseMove:
                mx = mouse_move.x;
                my = mouse_move.y;
                break;
            default: return true;
        }

        return mx >= xmin && my >= ymin && mx <= xmax && my <= ymax;
    }

    Event offset(int x, int y) const
    {
        Event e = *this;

        switch (kind)
        {
            case Kind::MouseWheel:
                e.mouse_wheel.x += x;
                e.mouse_wheel.y += y;
                break;
            case Kind::MouseButtonDown:
            case Kind::MouseButtonUp:
            case Kind::MouseButtonDoubleClick:
                e.mouse_button.x += x;
                e.mouse_button.y += y;
                break;
            case Kind::MouseMove:
                e.mouse_move.x += x;
                e.mouse_move.y += y;
                break;
            default: break;
        }

        return e;
    }

    static Event make_window_closed() { return Event{Kind::WindowClosed}; }

    static Event make_character(char c, bool repeated)
    {
        Event e = {Kind::Character};
        e.character.c = c;
        e.character.repeated = repeated;
        return e;
    }

    static Event make_key_up(Key key)
    {
        Event e = {Kind::KeyUp};
        e.key = key;
        return e;
    }

    static Event make_key_down(Key key)
    {
        Event e = {Kind::KeyDown};
        e.key = key;
        return e;
    }

    static Event make_mouse_move(int x, int y, int dx, int dy)
    {
        Event e = {Kind::MouseMove};
        e.mouse_move.x = x;
        e.mouse_move.y = y;
        e.mouse_move.dx = dx;
        e.mouse_move.dy = dy;
        return e;
    }

    static Event make_mouse_wheel(int x, int y, float d)
    {
        Event e = {Kind::MouseWheel};
        e.mouse_wheel.x = x;
        e.mouse_wheel.y = y;
        e.mouse_wheel.wheel = d;
        return e;
    }

    static Event make_mouse_button_up(MouseButton button, int x, int y)
    {
        Event e = {Kind::MouseButtonUp};
        e.mouse_button.button = button;
        e.mouse_button.x = x;
        e.mouse_button.y = y;
        return e;
    }

    static Event make_mouse_button_down(MouseButton button, int x, int y)
    {
        Event e = {Kind::MouseButtonDown};
        e.mouse_button.button = button;
        e.mouse_button.x = x;
        e.mouse_button.y = y;
        return e;
    }

    static Event make_mouse_leave() { return Event{Kind::MouseLeave}; }

    static Event make_mouse_button_double_click(MouseButton button, int x, int y)
    {
        Event e = {Kind::MouseButtonDoubleClick};
        e.mouse_button.button = button;
        e.mouse_button.x = x;
        e.mouse_button.y = y;
        return e;
    }

    static Event make_touch_start(int id, float x, float y)
    {
        Event e = {Kind::TouchStart};
        e.touch.id = id;
        e.touch.x = x;
        e.touch.y = y;
        return e;
    }

    static Event make_touch_end(int id, float x, float y)
    {
        Event e = {Kind::TouchEnd};
        e.touch.id = id;
        e.touch.x = x;
        e.touch.y = y;
        return e;
    }

    static Event make_touch_move(int id, float x, float y)
    {
        Event e = {Kind::TouchMove};
        e.touch.id = id;
        e.touch.x = x;
        e.touch.y = y;
        return e;
    }

    static Event make_touch_cancel(int id, float x, float y)
    {
        Event e = {Kind::TouchCancel};
        e.touch.id = id;
        e.touch.x = x;
        e.touch.y = y;
        return e;
    }

    static Event make_stylus_move(int x, int y)
    {
        Event e = {Kind::StylusMove};
        e.stylus.x = x;
        e.stylus.y = y;
        return e;
    }

    static Event make_stylus_up(int x, int y)
    {
        Event e = {Kind::StylusUp};
        e.stylus.x = x;
        e.stylus.y = y;
        return e;
    }

    static Event make_stylus_down(int x, int y)
    {
        Event e = {Kind::StylusDown};
        e.stylus.x = x;
        e.stylus.y = y;
        return e;
    }

    static Event make_stylus_leave() { return Event{Kind::StylusLeave}; }

    static Event make_window_resized(int w, int h)
    {
        Event e = {Kind::WindowResized};
        e.window_resized.width = w;
        e.window_resized.height = h;
        return e;
    }
};

} // namespace hrz::platform
