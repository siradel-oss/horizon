#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace data
{
struct SampleId;
class Database;
} // namespace data

namespace userdata
{
class Userdata;
}

namespace ui::context
{
struct Action
{
    virtual void execute() = 0;

    virtual ~Action() = default;

    static std::unique_ptr<Action> save_database();
    static std::unique_ptr<Action> load_database(const char* path = nullptr);
    static std::unique_ptr<Action> load_most_recent_database();
    static std::unique_ptr<Action> start_server();
    static std::unique_ptr<Action> inspect_gpu_snapshot(size_t index);
    static std::unique_ptr<Action> inspect_blob_snapshot(size_t index);
    static std::unique_ptr<Action> focus_in_timeline(double from, double to);
    static std::unique_ptr<Action> focus_in_timeline(data::SampleId, double from, double to);
    static std::unique_ptr<Action> highlight_frame_in_timeline(size_t frame_index);
    static std::unique_ptr<Action> highlight_frame_in_frame_graph(size_t frame_index);
    static std::unique_ptr<Action> inspect_sample(uint64_t record_hash, data::SampleId sample_id);
    static std::unique_ptr<Action> select_gpu_passes_frame(size_t frame_index);
};

class ActionBus
{
    friend struct ActionBusOwner;

public:
    ActionBus(std::vector<std::unique_ptr<Action>>& target) : _actions(&target) {}

    void push_back(std::unique_ptr<Action>&& action) { _actions->push_back(std::move(action)); }

private:
    std::vector<std::unique_ptr<Action>>* _actions;
};

class ActionBusOwner
{
public:
    ActionBus action_bus;

    ActionBusOwner() : action_bus(_actions) {}

    void execute_actions()
    {
        for (auto& action : _actions)
            action->execute();
    };

    void clear() { _actions.clear(); };

private:
    std::vector<std::unique_ptr<Action>> _actions;
};

enum class ApplicationRequest
{
    SoftQuit, // Ask for confirmation
    HardQuit, // Force quit
    Save,
    Load,
    LoadMostRecent,
    StartServer,
    AllowUserdataSaving,
    ForbidUserdataSaving
};

void initialize(const userdata::Userdata&);

void before_frame();
std::vector<ApplicationRequest> frame(const data::Database&, userdata::Userdata&);

void on_database_clear();

void show_quit_confirmation_dialog();
void show_userdata_loading_failure_dialog();

uint16_t get_requested_server_port();
std::string_view get_requested_data_path();

} // namespace ui::context
