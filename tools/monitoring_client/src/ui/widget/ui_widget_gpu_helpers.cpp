#include "ui/widget/ui_widget_gpu_helpers.h"

#include "ui/ui_helpers.h"

#include <imgui.h>

namespace ui::widget::gpu_helpers
{

using namespace helpers;

std::string gpu_snapshot_to_string(const data::GpuResourceSnapshot& snapshot)
{
    return fmt::format(
        "Snapshot #{} [{}] [{}]", snapshot.id, (Duration)snapshot.timestamp,
        (MemorySize)snapshot.get_total_size());
}

bool GpuBucketGroupOrderTabBar::draw()
{
    bool previous_value = _group_by_system;

    if (ImGui::BeginTabBar("##Grouping order tab bar"))
    {
        if (ImGui::BeginTabItem("By system"))
        {
            _group_by_system = true;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("By layer"))
        {
            _group_by_system = false;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    bool has_changed = previous_value != _group_by_system;
    if (has_changed)
    {
        _grouping_functions.clear();
        if (_group_by_system)
        {
            _grouping_functions.push_back([](const data::GpuResourceBucket& a)
                                          { return a.system; });
            _grouping_functions.push_back([](const data::GpuResourceBucket& a) { return a.layer; });
            _grouping_functions.push_back([](const data::GpuResourceBucket& a) { return a.type; });
        }
        else
        {
            _grouping_functions.push_back([](const data::GpuResourceBucket& a) { return a.layer; });
            _grouping_functions.push_back([](const data::GpuResourceBucket& a)
                                          { return a.system; });
            _grouping_functions.push_back([](const data::GpuResourceBucket& a) { return a.type; });
        }
    }

    return has_changed;
}

} // namespace ui::widget::gpu_helpers
