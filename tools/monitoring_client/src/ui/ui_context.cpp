#include "ui/ui_context.h"

#include "hrz/fnd/flat_hash_map.h"
#include "server.h"
#include "ui/common/ui_common_preset_menu.h"
#include "ui/ui_helpers.h"
#include "ui/ui_style.h"
#include "ui/widget/timeline/ui_widget_timeline.h"
#include "ui/widget/ui_widget_blob_inspector.h"
#include "ui/widget/ui_widget_frame_graph.h"
#include "ui/widget/ui_widget_gpu_comparator.h"
#include "ui/widget/ui_widget_gpu_treemap.h"
#include "ui/widget/ui_widget_histogram.h"
#include "ui/widget/ui_widget_metrics_pie.h"
#include "ui/widget/ui_widget_sample_inspector.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <lin_maths.h>
#include <sokol_app.h>

#include <array>
#include <cassert>
#include <memory>
#include <optional>
#include <vector>

namespace
{

using namespace ui;
using namespace context;
using namespace helpers;
using namespace preset_menu;

void _push_application_request(ApplicationRequest request);

enum class WindowType : uint32_t
{
    TimelineWindow = 0,
    GpuTreemapWindow,
    GpuComparisonWindow,
    SampleInspectorWindow,
    HistogramWindow,
    MetricsPieWindow,
    ServerInformationWindow,
    FrameGraphWindow,
    BlobInspectorWindow,
};

static constexpr size_t WindowTypeCount = (size_t)WindowType::BlobInspectorWindow + 1;

struct WindowDescriptor
{
    WindowType type;
    std::string name;
    ImGuiWindowFlags flags;
};

// Make descriptor indices match the enum value of the window type.
static const WindowDescriptor s_window_descriptors[] = {
    {WindowType::TimelineWindow, "Timeline", ImGuiWindowFlags_None},
    {WindowType::GpuTreemapWindow, "GPU treemap",
     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse},
    {WindowType::GpuComparisonWindow, "GPU comparison", ImGuiWindowFlags_HorizontalScrollbar},
    {WindowType::SampleInspectorWindow, "Sample inspector", ImGuiWindowFlags_None},
    {WindowType::HistogramWindow, "Histogram", ImGuiWindowFlags_None},
    {WindowType::MetricsPieWindow, "Metrics pie", ImGuiWindowFlags_None},
    {WindowType::ServerInformationWindow, "Server", ImGuiWindowFlags_None},
    {WindowType::FrameGraphWindow, "Frame graph", ImGuiWindowFlags_None},
    {WindowType::BlobInspectorWindow, "Blob inspector", ImGuiWindowFlags_None},
};

class Window
{
public:
    Window(std::string name, size_t key, ImGuiWindowFlags flags) :
        _name(name), _key(key), _flags(flags)
    {
    }

    virtual WindowType get_type() const = 0;

    const std::string& get_name() const { return _name; }

    size_t get_key() const { return _key; }

    void set_open(bool is_open) { _is_open = is_open; }

    bool is_open() const { return _is_open; }

    void process(
        const data::Database& database,
        userdata::Userdata& userdata,
        ActionBus& action_bus)
    {
        if (!is_open()) return;

        if (!begin())
        {
            ImGui::End();
            return;
        }

        Rect area = available_rect();
        _draw_contents(area, database, userdata, action_bus);

        ImGui::End();
    }

    bool begin()
    {
        ImGui::SetNextWindowSize(get_initial_size(), ImGuiCond_FirstUseEver);
        return ImGui::Begin(_name.c_str(), &_is_open, _flags);
    }

    virtual void on_database_clear() {}

protected:
    virtual const ImVec2 get_initial_size() const { return {300, 300}; }

private:
    // area is the window's pixel rect on the screen.
    // Drawing outside this area will enable window scrolling.
    virtual void _draw_contents(
        const Rect& area,
        const data::Database&,
        userdata::Userdata&,
        ActionBus&) = 0;

    std::string _name;
    size_t _key;
    ImGuiWindowFlags _flags;
    bool _is_open = true;
};

class TimelineWindow : public Window
{
public:
    TimelineWindow(std::string name, size_t key, ImGuiWindowFlags flags) : Window(name, key, flags)
    {
    }

    widget::Timeline timeline;

    WindowType get_type() const override { return WindowType::TimelineWindow; }

    void on_database_clear() override { timeline.on_database_clear(); }

protected:
    virtual const ImVec2 get_initial_size() const override { return {500, 300}; }

private:
    void _draw_contents(
        const Rect& area,
        const data::Database& database,
        userdata::Userdata& userdata,
        ActionBus& action_bus) override
    {
        timeline.draw(area, database, action_bus);
    }
};

class GpuTreemapWindow : public Window
{
public:
    GpuTreemapWindow(std::string name, size_t key, ImGuiWindowFlags flags) :
        Window(name, key, flags)
    {
    }

    widget::Treemap treemap;

    WindowType get_type() const override { return WindowType::GpuTreemapWindow; }

    void on_database_clear() override { treemap.on_database_clear(); }

private:
    void _draw_contents(
        const Rect& area,
        const data::Database& database,
        userdata::Userdata& userdata,
        ActionBus& action_bus) override
    {
        treemap.draw(area, database, userdata, action_bus);
    }
};

class GpuComparisonWindow : public Window
{
public:
    GpuComparisonWindow(std::string name, size_t key, ImGuiWindowFlags flags) :
        Window(name, key, flags)
    {
    }

    widget::GpuSnapshotComparator comparator;

    WindowType get_type() const override { return WindowType::GpuComparisonWindow; }

    void on_database_clear() override { comparator.on_database_clear(); }

private:
    void _draw_contents(
        const Rect& area,
        const data::Database& database,
        userdata::Userdata& userdata,
        ActionBus& action_bus) override
    {
        comparator.draw(area, database, action_bus);
    }
};

class SampleInspectorWindow : public Window
{
public:
    SampleInspectorWindow(std::string name, size_t key, ImGuiWindowFlags flags) :
        Window(name, key, flags)
    {
    }

    widget::SampleInspector sample_inspector;

    WindowType get_type() const override { return WindowType::SampleInspectorWindow; }

    void on_database_clear() override { sample_inspector.on_database_clear(); }

private:
    void _draw_contents(
        const Rect& area,
        const data::Database& database,
        userdata::Userdata& userdata,
        ActionBus& action_bus) override
    {
        sample_inspector.draw(area, database, action_bus);
    }
};

class HistogramWindow : public Window
{
public:
    HistogramWindow(std::string name, size_t key, ImGuiWindowFlags flags) : Window(name, key, flags)
    {
    }

    widget::Histogram histogram;

    WindowType get_type() const override { return WindowType::HistogramWindow; }

private:
    void _draw_contents(
        const Rect& area,
        const data::Database& database,
        userdata::Userdata& userdata,
        ActionBus& action_bus) override
    {
        histogram.draw(area, database, action_bus);
    }
};

class MetricsPieWindow : public Window
{
public:
    MetricsPieWindow(std::string name, size_t key, ImGuiWindowFlags flags) :
        Window(name, key, flags)
    {
    }

    widget::MetricsPie pie;

    WindowType get_type() const override { return WindowType::MetricsPieWindow; }

    void on_database_clear() override { pie.on_database_clear(); }

private:
    void _draw_contents(
        const Rect& area,
        const data::Database& database,
        userdata::Userdata& userdata,
        ActionBus& action_bus) override
    {
        pie.draw(area, database, action_bus);
    }
};

class ServerInformationWindow : public Window
{
public:
    ServerInformationWindow(std::string name, size_t key, ImGuiWindowFlags flags) :
        Window(name, key, flags)
    {
    }

    WindowType get_type() const override { return WindowType::ServerInformationWindow; }

protected:
    virtual const ImVec2 get_initial_size() const override { return {300, 100}; }

private:
    void _draw_contents(
        const Rect& area,
        const data::Database& database,
        userdata::Userdata& userdata,
        ActionBus& action_bus) override
    {
        bool show_database_clear_checkbox = false;
        bool show_stop_server_button = false;
        const char* start_server_button =
            (database.empty()) ? "Start server..." : "Erase session and start server...";

        switch (server::status())
        {
            case server::State::ClientConnected:
                ImGui::Text("Client connected - ");
                ImGui::SameLine(0.0F, 0.0F);
                if (database.get_horizon_version().empty())
                {
                    ImGui::TextDisabled("version was not communicated.");
                }
                else
                {
                    ImGui::Text("version: %s", database.get_horizon_version().c_str());
                }
                show_database_clear_checkbox = true;
                break;

            case server::State::Off:
                ImGui::TextDisabled("There is no server currently running.");
                if (ImGui::Button(start_server_button))
                {
                    action_bus.push_back(Action::start_server());
                }
                break;

            case server::State::WaitingForConnection:
                ImGui::Text("Listening on port %d - ", server::port());
                ImGui::SameLine(0.0F, 0.0F);
                if (server::has_session_id() && !server::is_database_clear_allowed())
                {
                    ImGui::TextColored(
                        ImColor(color::WARNING),
                        "The previous client has disconnected: connections from other clients will "
                        "be ignored.");
                    show_stop_server_button = true;
                }
                else
                {
                    ImGui::TextDisabled("Waiting for a client to connect...");
                }
                show_database_clear_checkbox = true;
                break;
        }

        if (show_database_clear_checkbox)
        {
            bool clear_allowed = server::is_database_clear_allowed();
            ImGui::Checkbox("Allow database to be automatically cleared", &clear_allowed);
            ImGui::SameLine();
            help_marker(
                "If enabled, any client will be able to connect "
                "after the previous one has disconnected.\n"
                "The previous session's data will be automatically cleared "
                "if the client is different.");
            server::allow_database_clear(clear_allowed);
        }

        if (show_stop_server_button)
        {
            if (show_database_clear_checkbox) ImGui::SameLine(0.0F, 20.0F);
            if (ImGui::Button("Stop server")) server::close();
        }
    }
};

class FrameGraphWindow : public Window
{
public:
    FrameGraphWindow(std::string name, size_t key, ImGuiWindowFlags flags) :
        Window(name, key, flags)
    {
    }

    widget::FrameGraph frame_graph;

    WindowType get_type() const override { return WindowType::FrameGraphWindow; }

    void on_database_clear() override { frame_graph.on_database_clear(); }

protected:
    virtual const ImVec2 get_initial_size() const override { return {500, 300}; }

private:
    void _draw_contents(
        const Rect& area,
        const data::Database& database,
        userdata::Userdata& userdata,
        ActionBus& action_bus) override
    {
        frame_graph.draw(area, database, action_bus);
    }
};

class BlobInspectorWindow : public Window
{
public:
    BlobInspectorWindow(std::string name, size_t key, ImGuiWindowFlags flags) :
        Window(name, key, flags)
    {
    }

    widget::BlobInspector blob_inspector;

    WindowType get_type() const override { return WindowType::BlobInspectorWindow; }

    void on_database_clear() override { blob_inspector.on_database_clear(); }

private:
    void _draw_contents(
        const Rect& area,
        const data::Database& database,
        userdata::Userdata& userdata,
        ActionBus& action_bus) override
    {
        blob_inspector.draw(area, database, userdata, action_bus);
    }
};

struct StartServerModal
{
    bool open;
    uint16_t port = 8500;

    void process()
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));

        if (open)
        {
            ImGui::OpenPopup("Server settings");
            open = false;
        }

        if (ImGui::BeginPopupModal("Server settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::PushItemWidth(280);

            ImGui::Text("Enter the port to listen to:");
            ImGui::InputScalar("Port", ImGuiDataType_U16, &port);

            ImGui::Separator();

            if (server::error())
            {
                ImGui::TextDisabled("Last request status: %s", server::error_string());
            }

            if (ImGui::Button("Start"))
            {
                _push_application_request(ApplicationRequest::StartServer);
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::PopItemWidth();

            ImGui::EndPopup();
        }
    }
};

struct ClearDatabaseWarningModal
{
    bool open = false;

    const char* window_name = "Erase data";
    std::function<void()> confirm_callback = []() {};

    void process()
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));

        if (open)
        {
            ImGui::OpenPopup(window_name);
            open = false;
        }

        if (ImGui::BeginPopupModal(window_name, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(
                ImColor(color::WARNING_CRITICAL),
                "This action will erase everything in the current session.");
            ImGui::Text("If this is not OK, go back and save your session before proceeding.");

            ImGui::Separator();
            if (ImGui::Button("Destroy everything"))
            {
                confirm_callback();
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
};

struct QuitUnsavedModal
{
    bool open = false;

    void process()
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));

        if (open)
        {
            ImGui::OpenPopup("Quit");
            open = false;
        }

        if (ImGui::BeginPopupModal("Quit", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(ImColor(color::WARNING_CRITICAL), "You have unsaved data.");
            ImGui::Text("Are you sure you want to quit without saving?");
            ImGui::Separator();

            if (ImGui::Button("Yes")) _push_application_request(ApplicationRequest::HardQuit);
            ImGui::SameLine();

            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
};

struct UserdataWarningModal
{
    bool open = false;

    void process()
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));

        if (open)
        {
            ImGui::OpenPopup("User data information");
            open = false;
        }

        if (ImGui::BeginPopupModal(
                "User data information", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(
                (ImColor)color::ERROR, "User data contains some invalid information!");
            ImGui::Separator();

            ImGui::Text(
                "Usually, the userdata file is overwritten when closing the application,\n"
                "but you can choose to disable this action in case a backup is needed.");

            ImGui::Spacing();
            ImGui::Text(
                "If you choose to disable file save, be aware that presets you create \n"
                "during this session will be discarded when closing the application.");

            ImGui::Separator();
            if (ImGui::Button("Overwrite userdata as usual"))
            {
                _push_application_request(ApplicationRequest::AllowUserdataSaving);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Do not save userdata for this session"))
            {
                _push_application_request(ApplicationRequest::ForbidUserdataSaving);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
};

struct Context
{
    std::array<std::vector<bool>, WindowTypeCount> used_window_ids;
    std::vector<std::unique_ptr<Window>> windows;
    ImGuiID dockspace_id;

    bool show_imgui_demo_window = false;
    bool show_imgui_metrics_window = false;

    ActionBusOwner action_bus_owner;
    std::vector<ApplicationRequest> requests;
    std::string data_path;

    StartServerModal start_server_modal;
    ClearDatabaseWarningModal clear_database_warning_modal;
    QuitUnsavedModal quit_unsaved_modal;
    UserdataWarningModal userdata_warning_modal;

    bool should_apply_layout;
    std::optional<userdata::WindowLayoutPreset> layout_to_apply;

    bool database_is_empty;
};

std::unique_ptr<Context> s_context;

void _push_application_request(ApplicationRequest request)
{
    s_context->requests.push_back(request);
}

void _process_windows(const data::Database& database, userdata::Userdata& userdata)
{
    s_context->action_bus_owner.clear();

    for (auto it = s_context->windows.begin(); it != s_context->windows.end();)
    {
        auto& window = *it;

        if (!window->is_open())
        {
            s_context->used_window_ids.at((size_t)window->get_type())[window->get_key()] = false;
            it = s_context->windows.erase(it);
            continue;
        }

        window->process(database, userdata, s_context->action_bus_owner.action_bus);

        ++it;
    }

    if (s_context->show_imgui_demo_window)
    {
        ImGui::ShowDemoWindow(&s_context->show_imgui_demo_window);
    }
    if (s_context->show_imgui_metrics_window)
    {
        ImGui::SetNextWindowSize(lm::dvec2(450.0F, 500.0F), ImGuiCond_Once);
        ImGui::ShowMetricsWindow(&s_context->show_imgui_metrics_window);
    }

    s_context->start_server_modal.process();
    s_context->clear_database_warning_modal.process();
    s_context->quit_unsaved_modal.process();
    s_context->userdata_warning_modal.process();

    s_context->action_bus_owner.execute_actions();
}

std::unique_ptr<Window> _create_window(
    WindowType type,
    std::string name,
    size_t key,
    ImGuiWindowFlags flags)
{
    std::unique_ptr<Window> window;

    switch (type)
    {
        case WindowType::BlobInspectorWindow:
            window = std::make_unique<BlobInspectorWindow>(name, key, flags);
            break;
        case WindowType::FrameGraphWindow:
            window = std::make_unique<FrameGraphWindow>(name, key, flags);
            break;
        case WindowType::GpuComparisonWindow:
            window = std::make_unique<GpuComparisonWindow>(name, key, flags);
            break;
        case WindowType::GpuTreemapWindow:
            window = std::make_unique<GpuTreemapWindow>(name, key, flags);
            break;
        case WindowType::HistogramWindow:
            window = std::make_unique<HistogramWindow>(name, key, flags);
            break;
        case WindowType::MetricsPieWindow:
            window = std::make_unique<MetricsPieWindow>(name, key, flags);
            break;
        case WindowType::SampleInspectorWindow:
            window = std::make_unique<SampleInspectorWindow>(name, key, flags);
            break;
        case WindowType::ServerInformationWindow:
            window = std::make_unique<ServerInformationWindow>(name, key, flags);
            break;
        case WindowType::TimelineWindow:
            window = std::make_unique<TimelineWindow>(name, key, flags);
            break;
        default: assert(false); break;
    }

    return window;
}

Window* _create_window(const WindowDescriptor& descriptor, size_t key)
{
    auto& used_window_ids = s_context->used_window_ids.at((size_t)descriptor.type);

    if (used_window_ids.size() <= key)
    {
        used_window_ids.resize(key + 1, false);
    }

    assert(!used_window_ids.at(key));
    used_window_ids[key] = true;

    auto name = key > 0 ? fmt::format("{} #{}", descriptor.name.c_str(), key) : descriptor.name;
    s_context->windows.push_back(
        std::move(_create_window(descriptor.type, name, key, descriptor.flags)));

    return s_context->windows.back().get();
}

Window* _create_new_window(const WindowDescriptor& descriptor)
{
    auto& used_window_ids = s_context->used_window_ids.at((size_t)descriptor.type);
    size_t key = used_window_ids.size();

    for (size_t i = 0; i < used_window_ids.size(); ++i)
    {
        if (!used_window_ids.at(i))
        {
            key = i;
            break;
        }
    }

    return _create_window(descriptor, key);
}

Window* _get_window(WindowType type, size_t key)
{
    auto& used_window_ids = s_context->used_window_ids.at((size_t)type);

    if (used_window_ids.size() <= key || !used_window_ids.at(key))
    {
        return nullptr;
    }

    for (auto& window : s_context->windows)
    {
        if (window->get_type() == type && window->get_key() == key)
        {
            return window.get();
        }
    }

    assert(false);
    return nullptr;
}

Window* _get_or_create_window(const WindowDescriptor& descriptor, size_t key)
{
    auto window = _get_window(descriptor.type, key);
    if (window != nullptr) return window;

    return _create_window(descriptor, key);
}

void _init_style(size_t style_index)
{
    ImGui::GetStyle().FrameRounding = 2;
    ImGui::SetColorEditOptions(
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_PickerHueWheel);

    style::apply(style_index);
}

userdata::WindowLayoutPreset _compute_current_layout()
{
    userdata::WindowLayoutPreset layout;

    layout.imgui_ini_data = ImGui::SaveIniSettingsToMemory();

    layout.windows.resize(s_context->windows.size());

    for (size_t i = 0; i < s_context->windows.size(); ++i)
    {
        auto& window = s_context->windows[i];
        layout.windows[i].type = (uint32_t)window->get_type();
        layout.windows[i].name = window->get_name();
        layout.windows[i].key = window->get_key();
    }

    return layout;
}

void _apply_default_layout_next_frame()
{
    s_context->should_apply_layout = true;
    s_context->layout_to_apply = std::nullopt;
}

void _apply_layout_next_frame(const userdata::WindowLayoutPreset& layout)
{
    s_context->should_apply_layout = true;
    s_context->layout_to_apply = {layout};
}

// Should not be called during an ImGui frame (ImGui::LoadIniSettingsFromMemory)
void _apply_layout_now(const userdata::WindowLayoutPreset& layout)
{
    for (auto& window : s_context->windows)
    {
        window->set_open(false);
    }

    ImGui::LoadIniSettingsFromMemory(layout.imgui_ini_data.c_str());

    for (size_t i = 0; i < layout.windows.size(); ++i)
    {
        const auto& layout_window = layout.windows[i];

        if (layout_window.type >= WindowTypeCount)
        {
            continue;
        }

        const auto& descriptor = s_window_descriptors[layout_window.type];
        auto window = _get_or_create_window(descriptor, layout_window.key);
        window->set_open(true);
    }
}

void _apply_default_layout_now()
{
    for (auto& window : s_context->windows)
    {
        window->set_open(false);
    }

    ImGuiID dockspace_id = ImGui::DockSpaceOverViewport();
    s_context->dockspace_id = dockspace_id;

    // Clear previous layout
    ImGui::DockBuilderRemoveNodeChildNodes(dockspace_id);

    ImGuiID side_dock_id =
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.25F, nullptr, &dockspace_id);
    ImGuiID sidesub_dock_id =
        ImGui::DockBuilderSplitNode(side_dock_id, ImGuiDir_Down, 0.50F, nullptr, &side_dock_id);
    ImGuiID sub_dock_id =
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.4F, nullptr, &dockspace_id);
    ImGuiID footer_dock_id =
        ImGui::DockBuilderSplitNode(sub_dock_id, ImGuiDir_Down, 0.27F, nullptr, &sub_dock_id);

    auto create_window = [&](const WindowDescriptor& descriptor, size_t key, ImGuiID dock_id)
    {
        auto window = _get_or_create_window(descriptor, key);
        window->set_open(true);
        ImGui::DockBuilderDockWindow(window->get_name().c_str(), dock_id);
    };

    create_window(s_window_descriptors[(size_t)WindowType::BlobInspectorWindow], 0, dockspace_id);
    create_window(s_window_descriptors[(size_t)WindowType::GpuTreemapWindow], 0, dockspace_id);
    create_window(s_window_descriptors[(size_t)WindowType::GpuComparisonWindow], 0, dockspace_id);
    create_window(s_window_descriptors[(size_t)WindowType::SampleInspectorWindow], 0, dockspace_id);
    create_window(s_window_descriptors[(size_t)WindowType::TimelineWindow], 0, dockspace_id);
    create_window(s_window_descriptors[(size_t)WindowType::HistogramWindow], 0, side_dock_id);
    create_window(s_window_descriptors[(size_t)WindowType::MetricsPieWindow], 0, sidesub_dock_id);
    create_window(
        s_window_descriptors[(size_t)WindowType::ServerInformationWindow], 0, footer_dock_id);
    create_window(s_window_descriptors[(size_t)WindowType::FrameGraphWindow], 0, sub_dock_id);

    ImGui::SetWindowFocus(s_window_descriptors[(size_t)WindowType::TimelineWindow].name.c_str());

    ImGui::DockBuilderFinish(dockspace_id);
}

void _process_file_menu(const data::Database& database, userdata::Userdata& userdata)
{
    if (ImGui::BeginMenu("File"))
    {
        if (server::status() == server::State::Off)
        {
            if (ImGui::MenuItem("Start monitoring server...", nullptr, nullptr))
            {
                Action::start_server()->execute();
            }
        }
        else if (ImGui::MenuItem("Stop Server"))
        {
            server::close();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Save data...", "Ctrl+S"))
        {
            Action::save_database()->execute();
        }

        if (ImGui::MenuItem("Load data...", "Ctrl+O"))
        {
            Action::load_database()->execute();
        }

        if (ImGui::BeginMenu("Load recent"))
        {
            const char* shortcut = "Ctrl+Maj+T";
            for (const auto& path : userdata.recently_opened_paths.get_elements())
            {
                if (ImGui::MenuItem(path, shortcut)) Action::load_database(path)->execute();
                shortcut = "";
            }

            if (userdata.recently_opened_paths.get_elements().empty())
            {
                ImGui::TextDisabled("Nothing to show here!");
            }
            else
            {
                ImGui::Separator();
                if (ImGui::MenuItem("Clear history")) userdata.recently_opened_paths.clear();
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Quit", "Alt+F4", nullptr))
        {
            _push_application_request(ApplicationRequest::SoftQuit);
        }
        ImGui::EndMenu();
    }
}

void _process_view_menu(userdata::Userdata& userdata)
{
    if (ImGui::BeginMenu("View"))
    {
        auto& buffer = ui::helpers::static_fmt_memory_buffer();

        for (const auto& descriptor : s_window_descriptors)
        {
            ui::helpers::format_buffer(buffer, "Add {}", descriptor.name.c_str());
            if (ImGui::MenuItem(buffer.data()))
            {
                _create_new_window(descriptor);
            }
        }

        ImGui::Separator();
        ImGui::MenuItem(
            "Show Dear ImGui Demo Window", "Ctrl+D", &s_context->show_imgui_demo_window);
        ImGui::MenuItem(
            "Show Dear ImGui Metrics Window", "Ctrl+M", &s_context->show_imgui_metrics_window);

        ImGui::Separator();
        if (ImGui::BeginMenu("Appearance"))
        {
            const auto names = style::get_names();
            for (size_t i = 0; i < names.size(); ++i)
            {
                if (ImGui::MenuItem(names[i]))
                {
                    style::apply(i);
                    userdata.style_index = i;
                }
            }

            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }
}

void _process_layout_menu(userdata::Userdata& userdata)
{
    auto& preset_map = userdata.window_layout_presets;

    auto menu_result = preset_system_menu(preset_map, true, "Layout");

    switch (menu_result.action)
    {
        case PresetAction::Select:
            if (menu_result.preset_name != "")
            {
                _apply_layout_next_frame(preset_map.at(menu_result.preset_name));
            }
            else
            {
                _apply_default_layout_next_frame();
            }
            userdata.layout_name = menu_result.preset_name;
            break;
        case PresetAction::Save:
            preset_map[menu_result.preset_name] = _compute_current_layout();
            userdata.layout_name = menu_result.preset_name;
            break;
        case PresetAction::Erase: preset_map.erase(menu_result.preset_name); break;
        default: break;
    }
}

void _process_main_menu_bar(const data::Database& database, userdata::Userdata& userdata)
{
    if (ImGui::BeginMainMenuBar())
    {
        _process_file_menu(database, userdata);
        _process_view_menu(userdata);
        _process_layout_menu(userdata);

        ImGui::Text("Duration: %.2lf ms", sapp_frame_duration() * 1000.0);

        ImGui::EndMainMenuBar();
    }
}

void _process_shortcuts()
{
    // Shortcut system:
    // For now, ImGui does not support keyboard shortcuts for menu items (they
    // are just for show).
    // We handle them here, but by doing so we duplicate the code already
    // responsible for clicking the corresponding menu item. It seems to
    // be the only way to achieve this.
    // See https://github.com/ocornut/imgui/issues/456#issuecomment-943344551
    // UPDATE:
    // A new function was added to a more recent version of imgui to handle shortcuts:
    // https://github.com/ocornut/imgui/issues/456#issuecomment-1307747841

    // Ctrl+S: save database to file
    if (ImGui::GetMergedKeyModFlags() == ImGuiKeyModFlags_Ctrl
        && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        Action::save_database()->execute();

        // Opening a file dialog using this method will cause ImGui to miss the
        // key release event.
        // As a fix, we want to manually register a key release event here.
        ImGui::GetIO().AddKeyEvent(ImGuiKey_S, false);
    }

    // Ctrl+O: load database from file
    if (ImGui::GetMergedKeyModFlags() == ImGuiKeyModFlags_Ctrl
        && ImGui::IsKeyPressed(ImGuiKey_O, false))
    {
        Action::load_database()->execute();
        ImGui::GetIO().AddKeyEvent(ImGuiKey_O, false);
    }

    // Ctrl+Maj+T: load database from most recent file
    if ((ImGui::GetMergedKeyModFlags() == (ImGuiKeyModFlags_Ctrl | ImGuiKeyModFlags_Shift))
        && ImGui::IsKeyPressed(ImGuiKey_T, false))
    {
        Action::load_most_recent_database()->execute();
    }

    // Ctrl+D: Toggle ImGui demo window
    if (ImGui::GetMergedKeyModFlags() == ImGuiKeyModFlags_Ctrl
        && ImGui::IsKeyPressed(ImGuiKey_D, false))
    {
        s_context->show_imgui_demo_window = !s_context->show_imgui_demo_window;
    }

    // Ctrl+M: Toggle ImGui metrics window
    // (AZERTY "M "is QWERTY ";")
    if (ImGui::GetMergedKeyModFlags() == ImGuiKeyModFlags_Ctrl
        && ImGui::IsKeyPressed(ImGuiKey_Semicolon, false))
    {
        s_context->show_imgui_metrics_window = !s_context->show_imgui_metrics_window;
    }
}

void _foreach_window(
    WindowType type,
    std::function<void(Window* window)> callback,
    bool focus = true)
{
    bool has_focused_window = false;

    for (auto& window : s_context->windows)
    {
        if (window->get_type() == type)
        {
            callback(window.get());

            if (!has_focused_window && focus)
            {
                ImGui::SetWindowFocus(window->get_name().c_str());
                has_focused_window = true;
            }
        }
    }
}

} // namespace

namespace ui::context
{

void initialize(const userdata::Userdata& userdata)
{
    s_context = std::make_unique<Context>();

    _init_style(userdata.style_index);

    auto preset_it = userdata.window_layout_presets.find(userdata.layout_name);
    if (preset_it != userdata.window_layout_presets.end())
    {
        _apply_layout_next_frame(preset_it->second);
    }
    else
    {
        _apply_default_layout_next_frame();
    }
}

void before_frame()
{
    if (s_context->should_apply_layout && s_context->layout_to_apply.has_value())
    {
        _apply_layout_now(s_context->layout_to_apply.value());

        s_context->should_apply_layout = false;
        s_context->layout_to_apply = std::nullopt;
    }
}

void remove_empty_dock_nodes(ImGuiDockNode* node)
{
    if (node == nullptr) return;

    for (size_t i = 0; i < 2; ++i)
    {
        remove_empty_dock_nodes(node->ChildNodes[i]);
    }

    if (node->IsEmpty())
    {
        ImGui::DockBuilderRemoveNode(node->ID);
    }
}

std::vector<ApplicationRequest> frame(const data::Database& database, userdata::Userdata& userdata)
{
    s_context->requests.clear();
    s_context->database_is_empty = database.empty();

    if (s_context->should_apply_layout && !s_context->layout_to_apply.has_value())
    {
        _apply_default_layout_now();

        s_context->should_apply_layout = false;
    }
    else
    {
        ImGui::DockSpaceOverViewport();
    }

    _process_main_menu_bar(database, userdata);
    _process_windows(database, userdata);
    _process_shortcuts();

    return s_context->requests;
}

void on_database_clear()
{
    for (auto& window : s_context->windows)
    {
        window->on_database_clear();
    }
}

void show_quit_confirmation_dialog()
{
    s_context->quit_unsaved_modal.open = true;
}

void show_userdata_loading_failure_dialog()
{
    s_context->userdata_warning_modal.open = true;
}

uint16_t get_requested_server_port()
{
    return s_context->start_server_modal.port;
}

std::string_view get_requested_data_path()
{
    return s_context->data_path;
}

struct OpenDatabaseClearWarningModal : public Action
{
    const char* window_name = "Erase database";
    std::function<void()> confirm_callback = []() { s_context->database_is_empty = true; };

    OpenDatabaseClearWarningModal(
        const char* param_window_name,
        std::optional<std::function<void()>> param_confirm_callback)
    {
        if (param_window_name) window_name = param_window_name;
        if (param_confirm_callback) confirm_callback = *param_confirm_callback;
    }

    void execute() override
    {
        s_context->clear_database_warning_modal.window_name = window_name;
        s_context->clear_database_warning_modal.confirm_callback = confirm_callback;
        s_context->clear_database_warning_modal.open = true;
    }
};

struct StartServer : public Action
{
    void execute() override { s_context->start_server_modal.open = true; }
};

struct SaveDatabase : public Action
{
    void execute() override { _push_application_request(ApplicationRequest::Save); }
};

struct LoadDatabase : public Action
{
    const char* path;

    LoadDatabase(const char* path = nullptr) : path(path) {}

    void execute() override
    {
        s_context->data_path = (path) ? path : "";
        s_context->requests.push_back(ApplicationRequest::Load);
    }
};

struct LoadMostRecentDatabase : public Action
{
    void execute() override { _push_application_request(ApplicationRequest::LoadMostRecent); }
};

struct InspectGpuSnapshot : public Action
{
    size_t index;

    InspectGpuSnapshot(size_t index) : index(index) {}

    void execute() override
    {
        _foreach_window(
            WindowType::GpuTreemapWindow, [&](Window* window)
            { ((GpuTreemapWindow*)window)->treemap.set_gpu_snapshot_index(index); });
    }
};

struct InspectBlobSnapshot : public Action
{
    size_t index;

    InspectBlobSnapshot(size_t index) : index(index) {}

    void execute() override
    {
        _foreach_window(
            WindowType::BlobInspectorWindow, [&](Window* window)
            { ((BlobInspectorWindow*)window)->blob_inspector.set_blob_snapshot_index(index); });
    }
};

struct FocusOnPeriodInTimeline : public Action
{
    double from;
    double to;

    FocusOnPeriodInTimeline(double from, double to) : from(from), to(to) {}

    void execute() override
    {
        _foreach_window(
            WindowType::TimelineWindow,
            [&](Window* window) { ((TimelineWindow*)window)->timeline.set_focus(from, to); });
    }
};

struct FocusOnSampleInTimeline : public Action
{
    data::SampleId sample_id;
    double entry;
    double exit;

    FocusOnSampleInTimeline(data::SampleId id, double entry, double exit) :
        sample_id(id), entry(entry), exit(exit)
    {
    }

    void execute() override
    {
        _foreach_window(
            WindowType::TimelineWindow, [&](Window* window)
            { ((TimelineWindow*)window)->timeline.set_focus(sample_id, entry, exit); });
    }
};

struct InspectSample : public Action
{
    uint64_t record_hash;
    data::SampleId sample_id;

    InspectSample(uint64_t hash, data::SampleId id) : record_hash(hash), sample_id(id) {}

    void execute() override
    {
        _foreach_window(
            WindowType::SampleInspectorWindow,
            [&](Window* window)
            {
                ((SampleInspectorWindow*)window)
                    ->sample_inspector.focus_on_sample(record_hash, sample_id);
            });
    }
};

struct SelectGpuPassesFrame : public Action
{
    size_t frame_index;

    SelectGpuPassesFrame(size_t index) : frame_index(index) {}

    void execute() override
    {
        _foreach_window(
            WindowType::MetricsPieWindow,
            [&](Window* window)
            {
                ((MetricsPieWindow*)window)->pie.select_frame(frame_index);
                ((MetricsPieWindow*)window)->pie.display_mode =
                    widget::MetricsPie::DisplayMode::PieChart;
            });
    }
};

struct HighlightFrameInTimeline : public Action
{
    size_t frame_index;

    HighlightFrameInTimeline(size_t index) : frame_index(index) {};

    void execute() override
    {
        _foreach_window(
            WindowType::TimelineWindow, [&](Window* window)
            { ((TimelineWindow*)window)->timeline.set_highlighted_frame(frame_index); }, false);
    }
};

struct HighlightFrameInFrameGraph : public Action
{
    size_t frame_index;

    HighlightFrameInFrameGraph(size_t index) : frame_index(index) {};

    void execute() override
    {
        _foreach_window(
            WindowType::FrameGraphWindow, [&](Window* window)
            { ((FrameGraphWindow*)window)->frame_graph.set_highlighted_frame(frame_index); },
            false);
    }
};

std::unique_ptr<Action> Action::start_server()
{
    if (!s_context->database_is_empty)
    {
        return std::make_unique<OpenDatabaseClearWarningModal>(
            "Start server", []() { StartServer().execute(); });
    }
    return std::make_unique<StartServer>();
}

std::unique_ptr<Action> Action::save_database()
{
    return std::make_unique<SaveDatabase>();
}

std::unique_ptr<Action> Action::load_database(const char* data_path)
{
    if (!s_context->database_is_empty)
    {
        return std::make_unique<OpenDatabaseClearWarningModal>(
            "Load data", [data_path]() { LoadDatabase(data_path).execute(); });
    }
    return std::make_unique<LoadDatabase>(data_path);
}

std::unique_ptr<Action> Action::load_most_recent_database()
{
    return std::make_unique<LoadMostRecentDatabase>();
}

std::unique_ptr<Action> Action::inspect_gpu_snapshot(size_t index)
{
    return std::make_unique<InspectGpuSnapshot>(index);
}

std::unique_ptr<Action> Action::inspect_blob_snapshot(size_t index)
{
    return std::make_unique<InspectBlobSnapshot>(index);
}

std::unique_ptr<Action> Action::focus_in_timeline(double from, double to)
{
    return std::make_unique<FocusOnPeriodInTimeline>(from, to);
}

std::unique_ptr<Action> Action::focus_in_timeline(data::SampleId id, double entry, double exit)
{
    return std::make_unique<FocusOnSampleInTimeline>(id, entry, exit);
}

std::unique_ptr<Action> Action::inspect_sample(uint64_t record_hash, data::SampleId sample_id)
{
    return std::make_unique<InspectSample>(record_hash, sample_id);
}

std::unique_ptr<Action> Action::select_gpu_passes_frame(size_t frame_index)
{
    return std::make_unique<SelectGpuPassesFrame>(frame_index);
}

std::unique_ptr<Action> Action::highlight_frame_in_timeline(size_t frame_index)
{
    return std::make_unique<HighlightFrameInTimeline>(frame_index);
}

std::unique_ptr<Action> Action::highlight_frame_in_frame_graph(size_t frame_index)
{
    return std::make_unique<HighlightFrameInFrameGraph>(frame_index);
}

} // namespace ui::context
