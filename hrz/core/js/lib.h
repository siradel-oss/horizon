#include <stdint.h>

extern "C"
{
    extern char* hrz_js_get_user_agent();

    extern char* hrz_js_get_base_url();

    typedef void (
        *hrz_js_fetch_data_onload_func)(int handle, int http_code, void* ptr_arg, int int_arg);

    typedef void (*hrz_js_fetch_data_onerror_func)(
        int handle,
        int http_code,
        const char* error,
        void* ptr_arg,
        int int_arg);

    extern int hrz_js_fetch_data(
        const char* url,
        // We use doubles here because int may be too small, and passing uint64s is annoying.
        double range_start,
        double range_size,
        const char* headers_json,
        const char* requesttype,
        const char* param,
        hrz_js_fetch_data_onload_func onload,
        hrz_js_fetch_data_onerror_func onerror,
        void* ptr_arg,
        int int_arg);

    /**
     * Fetch all header as a Json string representing an array of strings.
     * The headers are written as [name0, value0, name1, value1, name2, value2, ...].
     * Do not call before the headers have been received.
     * Do not free the returned string.
     */
    extern const char* hrz_js_fetch_get_headers(int handle);

    /**
     * Return the size in bytes of the downloaded data.
     */
    extern unsigned int hrz_js_fetch_get_data_size(int handle);

    /**
     * Load downloaded data into Emscripten's heap.
     * The caller is responsible for freeing the pointer afterwards.
     */
    extern void* hrz_js_fetch_get_data(int handle);

    /**
     * Copy downloaded data into Emscripten's heap, at the provided location.
     */
    extern bool hrz_js_fetch_copy_data(int handle, const void* data, unsigned int data_size);

    extern void hrz_js_fetch_abort(int handle);

    extern void hrz_js_fetch_clean(int handle);

    typedef void (*hrz_js_pointer_callback_func)(
        int pointerId,
        int pointerType,
        float x,
        float y,
        int buttons,
        int mod_keys, // bit 0 is ctrl, 1 shift, 2 alt
        void* userData);

    extern void hrz_js_set_pointer_down_handler(
        const char* target_id,
        void* userData,
        bool capture,
        hrz_js_pointer_callback_func callback);
    extern void hrz_js_set_pointer_up_handler(
        const char* target_id,
        void* userData,
        bool capture,
        hrz_js_pointer_callback_func callback);
    extern void hrz_js_set_pointer_move_handler(
        const char* target_id,
        void* userData,
        bool capture,
        hrz_js_pointer_callback_func callback);
    extern void hrz_js_set_pointer_cancel_handler(
        const char* target_id,
        void* userData,
        bool capture,
        hrz_js_pointer_callback_func callback);
    extern void hrz_js_set_pointer_leave_handler(
        const char* target_id,
        void* userData,
        bool capture,
        hrz_js_pointer_callback_func callback);

    extern void hrz_js_copy_string_to_clipboard(const char* str);

    extern bool hrz_js_register_canvas(const char* canvas_selector);

    typedef void (*hrz_js_resize_observer_callback_func)(
        float css_width,
        float css_height,
        float device_width,
        float device_height,
        void* userData);

    extern bool hrz_js_install_resize_observer(
        hrz_js_resize_observer_callback_func cb,
        void* user_data);

    extern void hrz_js_uninstall_resize_observer();

    extern void hrz_js_install_module_leak_fix();

    extern float hrz_js_get_device_pixel_ratio();
}
