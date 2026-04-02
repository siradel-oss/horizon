#pragma once

#include "data.h"

#include <vector>

namespace ui::widget::gpu_helpers
{

std::string gpu_snapshot_to_string(const data::GpuResourceSnapshot&);

class GpuBucketGroupOrderTabBar
{
public:
    GpuBucketGroupOrderTabBar() {}

    bool draw();

    const std::vector<data::GpuResourceBucketGroupingFunction>& get_grouping_functions() const
    {
        return _grouping_functions;
    }

    bool is_grouped_by_systems() { return _group_by_system; }

private:
    bool _group_by_system;
    std::vector<data::GpuResourceBucketGroupingFunction> _grouping_functions;
};

} // namespace ui::widget::gpu_helpers
