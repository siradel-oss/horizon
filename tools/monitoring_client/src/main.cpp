// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#include "data.h"
#include "hrz/monitoring/monitoring.h"
#include "server.h"
#include "ui/ui_context.h"
#include "userdata.h"

#include <monitoring_client_resources.h>
#include <stb_image.h>
#include <ws_server.h>

#define SOKOL_IMPL
#define SOKOL_GLCORE33
#include <imgui.h>
#include <portable-file-dialogs.h>
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_imgui.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <locale>
#include <span>

constexpr const char* WINDOW_TITLE = "Horizon Monitoring";
constexpr const char* WINDOW_TITLE_UNSAVED = "(*)Horizon Monitoring";

sg_pass_action _pass_action;

data::Database _database;
userdata::Userdata _userdata;

int _server_port = 8500;
bool _save_userdata = true;

void init(void)
{
    std::locale::global(std::locale("en_US.UTF-8"));

    sg_desc desc = {};
    desc.context = sapp_sgcontext();
    sg_setup(&desc);

    simgui_desc_t simgui_desc = {};
    simgui_desc.disable_paste_override = false;
    simgui_desc.no_default_font = true;
    simgui_setup(&simgui_desc);

    // ImGui config - enable docking and load custom font
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImFontConfig fontCfg;
    std::snprintf(fontCfg.Name, sizeof(fontCfg.Name), "Rubik Regular");
    fontCfg.FontDataOwnedByAtlas = false;
    fontCfg.OversampleH = 2;
    fontCfg.OversampleV = 2;
    fontCfg.RasterizerMultiply = 1.5F;

    auto font_data = res::get_data(res::Resources::Font);
    io.Fonts->AddFontFromMemoryTTF((void*)font_data.data(), font_data.size(), 14.0F, &fontCfg);

    unsigned char* font_pixels;
    int font_width, font_height;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    sg_image_desc img_desc = {};
    img_desc.width = font_width;
    img_desc.height = font_height;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    img_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    img_desc.min_filter = SG_FILTER_LINEAR;
    img_desc.mag_filter = SG_FILTER_LINEAR;
    img_desc.data.subimage[0][0].ptr = font_pixels;
    img_desc.data.subimage[0][0].size = font_width * font_height * 4;

    io.Fonts->TexID = (ImTextureID)(uintptr_t)sg_make_image(&img_desc).id;

    _pass_action.colors[0].action = SG_ACTION_CLEAR;
    _pass_action.colors[0].value = {0.90F, 0.90F, 0.90F, 1.0F};

    auto parsing_error = _userdata.load_from_disk();

    ui::context::initialize(_userdata);

    // Missing data in the userdata file is normal behaviour when opening a new
    // version of the application that uses new data, so we don't want to trigger
    // the failure dialog in this case
    if (parsing_error & userdata::ParsingError_InvalidData)
    {
        ui::context::show_userdata_loading_failure_dialog();
        _save_userdata = false; // Disable auto saving of userdata at cleanup
                                // to avoid accidental overriding when quitting
                                // the app without dealing with the dialog
    }

    server::start(_server_port);
}

void cleanup(void)
{
    if (server::status() != server::State::Off)
    {
        server::close();
    }

    if (_save_userdata)
    {
        bool saved = _userdata.save_to_disk();
        if (!saved)
        {
            std::cerr << "Failed to save userdata!" << std::endl;
        }
    }

    sg_shutdown();
}

void input(const sapp_event* event)
{
    simgui_handle_event(event);

    if (event->type == SAPP_EVENTTYPE_QUIT_REQUESTED && _database.has_unsaved_data())
    {
        ui::context::show_quit_confirmation_dialog();
        sapp_cancel_quit();
    }
}

std::string_view find_file_extension(std::string_view filepath)
{
    std::string_view::size_type pos = filepath.find_last_of(".");
    return (pos == std::string_view::npos) ? "" : filepath.substr(pos);
}

void save_data(const char* path)
{
    bool success = _database.save_to_file(path);
    if (!success)
    {
        std::string error_desc = "Error: the file at ";
        error_desc += path;
        error_desc += " could not be written to.";

        pfd::message("Error", error_desc, pfd::choice::ok, pfd::icon::error);
    }
    else
    {
        _userdata.recently_opened_paths.push_element(path);
    }
}

void load_data(const char* path)
{
    _database.clear();
    ui::context::on_database_clear();

    server::close();

    bool success = _database.load_from_file(path);
    if (!success)
    {
        std::string error_desc = "Error: the file at ";
        error_desc += path;
        error_desc +=
            " could not be opened.\n\n"
            "Remove it from the list of recent paths?";

        auto message = pfd::message("Error", error_desc, pfd::choice::yes_no, pfd::icon::error);
        if (message.result() == pfd::button::yes)
        {
            _userdata.recently_opened_paths.remove_element(path);
        }
    }
    else
    {
        _userdata.recently_opened_paths.push_element(path);
    }
}

void save_dialog()
{
    std::string filepath =
        pfd::save_file("Save file", "", {"Horizon profile", "*.hrz_profile", "All files", "*"})
            .result();
    if (!filepath.empty())
    {
        if (find_file_extension(filepath).empty()) filepath += ".hrz_profile";

        save_data(filepath.c_str());
    }
}

void load_dialog()
{
    auto result =
        pfd::open_file("Open file", "", {"Horizon profile", "*.hrz_profile", "All files", "*"})
            .result();
    if (!result.empty())
    {
        if (result.size() > 1)
        {
            pfd::message(
                "Error", "Please select one file only.", pfd::choice::ok, pfd::icon::error);
        }
        else
        {
            load_data(result[0].c_str());
        }
    }
}

void frame(void)
{
    ui::context::before_frame();

    const int width = sapp_width();
    const int height = sapp_height();
    simgui_new_frame({width, height, sapp_frame_duration(), sapp_dpi_scale()});

    if (server::status() != server::State::Off)
    {
        server::poll(_database);
    }

    const auto requests = ui::context::frame(_database, _userdata);

    bool open_save_dialog = false;
    bool open_load_dialog = false;

    for (ui::context::ApplicationRequest request : requests)
    {
        switch (request)
        {
            case ui::context::ApplicationRequest::SoftQuit: sapp_request_quit(); break;

            case ui::context::ApplicationRequest::HardQuit: sapp_quit(); break;

            case ui::context::ApplicationRequest::Save: open_save_dialog = true; break;

            case ui::context::ApplicationRequest::Load:
            {
                const auto data_path = ui::context::get_requested_data_path();

                if (data_path.empty())
                    open_load_dialog = true;
                else
                    load_data(data_path.data());

                break;
            }

            case ui::context::ApplicationRequest::LoadMostRecent:
            {
                if (!_userdata.recently_opened_paths.get_elements().empty())
                {
                    load_data(_userdata.recently_opened_paths.get_elements().front());
                }
                break;
            }

            case ui::context::ApplicationRequest::StartServer:
                _database.clear();
                ui::context::on_database_clear();
                server::start(ui::context::get_requested_server_port());
                break;

            case ui::context::ApplicationRequest::AllowUserdataSaving: _save_userdata = true; break;

            case ui::context::ApplicationRequest::ForbidUserdataSaving:
                _save_userdata = false;
                break;

            default:;
        }
    }

    // the sokol_gfx draw pass
    sg_begin_default_pass(&_pass_action, width, height);
    simgui_render();
    sg_end_pass();
    sg_commit();

    // We delay the opening of save and open dialogs until after the frame has been rendered.
    // This is to prevent an issue where moving or resizing the appplication window when a
    // dialog is opened would crash the application, because ImGui's BeginFrame would
    // get called before the necessary EndFrame. This seems to be an issue on sokol's
    // end, so other issues might emerge in the future by using this file dialog library.
    if (open_save_dialog)
    {
        save_dialog();
    }

    if (open_load_dialog)
    {
        load_dialog();
    }

    sapp_set_window_title((_database.has_unsaved_data()) ? WINDOW_TITLE_UNSAVED : WINDOW_TITLE);
}

void load_icon_data(res::Resources res_id, sapp_image_desc* img)
{
    auto icon_res = res::get_data(res_id);
    int icon_width;
    int icon_height;
    int channels_in_icon_file;
    stbi_uc* decoded_icon_data = stbi_load_from_memory(
        (const stbi_uc*)icon_res.data(), (int)icon_res.size_bytes(), &icon_width, &icon_height,
        &channels_in_icon_file, 4);

    img->width = icon_width;
    img->height = icon_height;
    img->pixels.ptr = decoded_icon_data;
    img->pixels.size = icon_width * icon_height * 4;
}

sapp_desc sokol_main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    sapp_icon_desc icon = {};
    icon.sokol_default = false;
    load_icon_data(res::Resources::Icon16, &icon.images[0]);
    load_icon_data(res::Resources::Icon32, &icon.images[1]);
    load_icon_data(res::Resources::Icon96, &icon.images[2]);

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = input;
    desc.enable_clipboard = true;
    desc.clipboard_size = 1000000;
    desc.window_title = WINDOW_TITLE;
    desc.icon = icon;
    desc.gl_force_gles2 = true;
    desc.win32_console_attach = true;
    desc.high_dpi = true;
    desc.swap_interval = 1;

    return desc;
}
