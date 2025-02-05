#ifdef _WIN32

#    include "wsi.h"

#    include <hrz_icons.h>

#    include <assert.h>
#    include <stb_image.h>
#    include <windows.h>

#    include <stdio.h>

#    define HRZ_VERSION_STR3(X) #X
#    define HRZ_VERSION_STR2(X) HRZ_VERSION_STR3(X)
#    define HRZ_VERSION_STR HRZ_VERSION_STR2(HRZ_VERSION)

void* stbi_malloc(size_t size)
{
    return malloc(size);
}

void* stbi_realloc(void* ptr, size_t new_size)
{
    return realloc(ptr, new_size);
}

void stbi_free(void* ptr)
{
    free(ptr);
}

namespace
{
const char* class_name = "Horizon window";
bool running = true;
HWND window;
} // namespace

static_assert(sizeof(HINSTANCE) == sizeof(void*), "HINSTANCE size");
static_assert(sizeof(HWND) == sizeof(void*), "HWND size");

// https://stackoverflow.com/a/62614596
static HICON create_icon_from_bytes(HDC DC, int width, int height, const uint32_t* bytes)
{
    HICON icon = NULL;

    ICONINFO icon_info = {
        TRUE, // fIcon, set to true if this is an icon, set to false if this is a cursor
        NULL, // xHotspot, set to null for icons
        NULL, // yHotspot, set to null for icons
        NULL, // Monochrome bitmap mask, set to null initially
        NULL  // Color bitmap mask, set to null initially
    };

    std::unique_ptr<uint32_t[]> raw_bitmap(new uint32_t[width * height]);

    ULONG u_width = (ULONG)width;
    ULONG u_height = (ULONG)height;
    uint32_t* bitmap_ptr = raw_bitmap.get();
    for (ULONG y = 0; y < u_height; y++)
    {
        for (ULONG x = 0; x < u_width; x++)
        {
            // Bytes are expected to be in RGB order (8 bits each)
            // Swap G and B bytes, so that it is in BGR order for windows
            uint32_t byte = bytes[x + y * width];
            uint8_t A = (byte & 0xff000000) >> 24;
            uint8_t R = (byte & 0x000000ff) >> 0;
            uint8_t G = (byte & 0x0000ff00) >> 8;
            uint8_t B = (byte & 0x00ff0000) >> 16;
            *bitmap_ptr = (A << 24) | (R << 16) | (G << 8) | B;
            bitmap_ptr++;
        }
    }

    icon_info.hbmColor = CreateBitmap(width, height, 1, 32, raw_bitmap.get());
    if (icon_info.hbmColor)
    {
        icon_info.hbmMask = CreateCompatibleBitmap(DC, width, height);
        if (icon_info.hbmMask)
        {
            icon = CreateIconIndirect(&icon_info);
            if (!icon)
            {
                printf("Failed to create icon.\n");
            }
            DeleteObject(icon_info.hbmMask);
        }
        else
        {
            printf("Failed to create color mask.\n");
        }
        DeleteObject(icon_info.hbmColor);
    }
    else
    {
        printf("Failed to create bitmap mask.\n");
    }

    return icon;
}

void set_window_icon(HWND window, WPARAM icon_id, int metric)
{
    hrz_icons::Resources res_id = hrz_icons::Resources::Icon96;

    int icon_size = GetSystemMetrics(metric);
    if (icon_size <= 16)
    {
        res_id = hrz_icons::Resources::Icon16;
    }
    else if (icon_size <= 32)
    {
        res_id = hrz_icons::Resources::Icon32;
    }

    auto icon_res = hrz_icons::get_data(res_id);
    int icon_width;
    int icon_height;
    int channels_in_icon_file;
    stbi_uc* decoded_icon_data = stbi_load_from_memory(
        (const stbi_uc*)icon_res.data(), (int)icon_res.size_bytes(), &icon_width, &icon_height,
        &channels_in_icon_file, 4);

    if (decoded_icon_data)
    {
        HICON icon = create_icon_from_bytes(
            GetWindowDC(window), icon_width, icon_height, (const uint32_t*)decoded_icon_data);

        SendMessage(window, WM_SETICON, icon_id, (LPARAM)icon);
        stbi_image_free(decoded_icon_data);
    }
}

std::optional<WsiInstance> wsi_init(int width, int height, bool show_window)
{
    HINSTANCE instance = GetModuleHandle(NULL);

    WNDCLASSA wnd_class = {};
    wnd_class.style = CS_HREDRAW | CS_VREDRAW;
    wnd_class.lpfnWndProc = DefWindowProc;
    wnd_class.hInstance = instance;
    wnd_class.lpszClassName = class_name;

    ATOM wnd_class_atom = RegisterClassA(&wnd_class);
    assert(wnd_class_atom);

    RECT rect = {0, 0, width, height};
    AdjustWindowRect(&rect, GWL_STYLE, FALSE);

    window = CreateWindowA(
        class_name, "Horizon " HRZ_VERSION_STR, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, instance, NULL);
    assert(window);

    set_window_icon(window, ICON_BIG, SM_CXICON);
    set_window_icon(window, ICON_SMALL, SM_CXSMICON);

    if (show_window)
    {
        ShowWindow(window, SW_SHOW);
    }

    return {WsiInstance{(void*)instance, (void*)window}};
}

void wsi_run(WsiRunFn fn)
{
    while (fn())
        ;
}

void wsi_cleanup() {}

#endif
