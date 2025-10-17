#include "ui/widget/ui_widget_gpu_comparator.h"

#include "ui/common/ui_common_array_selector.h"
#include "ui/ui_colors.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace
{
bool _table_name_column(const char* name, bool is_leaf, bool default_open)
{
    ImGui::TableNextColumn();
    if (is_leaf)
    {
        ImGui::BulletText("%s", name);
        return false;
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
    if (default_open) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    return ImGui::TreeNodeEx(name, flags);
}

void _table_group_size_column(const std::optional<data::GpuResourceBucketGroup>& group)
{
    ImGui::TableNextColumn();
    if (group)
    {
        auto& buffer = ui::helpers::static_fmt_memory_buffer();

        ui::helpers::format_buffer(buffer, "{}", (ui::helpers::MemorySize)group->total_size);
        ImGui::Text("%s", buffer.data());
    }
    else
    {
        ImGui::Text("---");
    }
}

void _table_group_comparison_column(
    const std::optional<data::GpuResourceBucketGroup>& group,
    const std::optional<data::GpuResourceBucketGroup>& reference_group,
    bool compare)
{
    ImGui::TableNextColumn();

    size_t group_size = (group) ? group->total_size : 0;
    size_t reference_size = (reference_group) ? reference_group->total_size : 0;

    if (compare && (group_size != 0 || reference_size != 0))
    {
        std::string display_str = "";
        int64_t difference = group_size - reference_size;

        if (difference != 0)
        {
            char op = (difference > 0) ? '+' : '-';
            size_t abs_difference = std::llabs(difference);
            fmt::format_to(
                std::back_inserter(display_str), "{} {}", op,
                (ui::helpers::MemorySize)abs_difference);
        }
        else
        {
            display_str = "==";
        }

        ImGui::Text("%s", display_str.c_str());
    }
    else
    {
        ImGui::Text("---");
    }
}

} // namespace

namespace ui::widget
{
using namespace helpers;
using namespace gpu_helpers;

void GpuSnapshotComparator::on_database_clear()
{
    _cells.clear();
    _snapshot_index = 0;
    _reference_index = std::nullopt;
}

void GpuSnapshotComparator::draw(
    const Rect& area,
    const data::Database& database,
    context::ActionBus& action_bus)
{
    if (database.get_gpu_snapshots().empty())
    {
        ImGui::TextDisabled("No GPU snapshot has been received yet.");
        return;
    }

    bool grouping_order_changed = _group_tab_bar.draw();
    if (grouping_order_changed)
    {
        _reset_cells = true;
    }

    auto grouping_functions = _group_tab_bar.get_grouping_functions();

    auto snapshot_count = database.get_gpu_snapshots().size();
    if (_snapshot_index >= snapshot_count)
    {
        _snapshot_index = snapshot_count - 1;
        _reset_cells = true;
    }
    if (_reference_index && *_reference_index >= snapshot_count)
    {
        _reference_index = std::nullopt;
        _reset_cells = true;
    }

    bool sort_scheduled = false;
    if (_cells.empty() || _reset_cells)
    {
        _reset_cells = false;
        _compute_cells(database.get_gpu_snapshots());
        sort_scheduled = true;
    }

    _draw_header(available_rect().p0, database.get_gpu_snapshots());

    bool in_table = _setup_table();
    if (in_table)
    {
        auto* sort_specs = ImGui::TableGetSortSpecs();
        if (sort_specs)
        {
            if (sort_scheduled || sort_specs->SpecsDirty)
            {
                sort_specs->SpecsDirty = false;
                _sort_cells(sort_specs);
            }
        }

        _recursive_cell_tree(0);

        ImGui::EndTable();
    }
}

void GpuSnapshotComparator::_compute_cells(std::span<const data::GpuResourceSnapshot> snapshots)
{
    const auto& snapshot = snapshots[_snapshot_index];
    std::vector<const data::GpuResourceBucket*> buckets = {};
    for (const auto& bucket : snapshot.get_buckets())
    {
        buckets.push_back(&bucket);
    }

    std::vector<const data::GpuResourceBucket*> ref_buckets = {};
    if (_reference_index)
    {
        const auto& reference_snapshot = snapshots[*_reference_index];
        for (const auto& bucket : reference_snapshot.get_buckets())
        {
            ref_buckets.push_back(&bucket);
        }
    }

    // We want to assign to the root cell the groups containing all of the snapshot
    // buckets, so that the total size of the two snapshots can be compared as well.
    std::optional<data::GpuResourceBucketGroup> group;
    std::optional<data::GpuResourceBucketGroup> ref_group;

    group = data::group_gpu_resource_buckets(
                buckets, [](const data::GpuResourceBucket& bucket) { return ""; })
                .front();

    if (!ref_buckets.empty())
    {
        ref_group = data::group_gpu_resource_buckets(
                        ref_buckets, [](const data::GpuResourceBucket& bucket) { return ""; })
                        .front();
    }

    _cells.clear();
    _cells.push_back({});
    _cells.front().first_child_id = 1;
    _cells.front().key = "TOTAL";
    _cells.front().group = group;
    _cells.front().reference_group = ref_group;

    const auto& grouping_functions = _group_tab_bar.get_grouping_functions();
    _recursive_compute_cell_children(0, buckets, ref_buckets, grouping_functions);
}

void GpuSnapshotComparator::_recursive_compute_cell_children(
    size_t parent_id,
    std::span<const data::GpuResourceBucket*> buckets,
    std::span<const data::GpuResourceBucket*> ref_buckets,
    std::span<const data::GpuResourceBucketGroupingFunction> grouping_functions)
{
    if (grouping_functions.empty())
    {
        return;
    }

    const auto& grouping_function = grouping_functions.front();

    const auto& groups = data::group_gpu_resource_buckets(buckets, grouping_function);
    const auto& ref_groups = data::group_gpu_resource_buckets(ref_buckets, grouping_function);

    // Create the cells corresponding to the groups
    hrz::flat_hash_map<std::string, size_t> created_cells;

    for (const auto& group : groups)
    {
        if (!created_cells.contains(group.key))
        {
            created_cells[group.key] = _cells.size();

            _cells.push_back({});
            _cells.back().key = group.key;
            _cells.back().group = group;
            _cells.back().id = _cells.size() - 1;
            _cells.back().depth = _cells.at(parent_id).depth + 1;

            _cells[parent_id].child_count++;
        }
    }

    for (const auto& group : ref_groups)
    {
        if (!created_cells.contains(group.key))
        {
            created_cells[group.key] = _cells.size();

            _cells.push_back({});
            _cells.back().key = group.key;
            _cells.back().reference_group = group;
            _cells.back().id = _cells.size() - 1;
            _cells.back().depth = _cells.at(parent_id).depth + 1;

            _cells[parent_id].child_count++;
        }
        else
        {
            size_t id = created_cells.at(group.key);
            _cells.at(id).reference_group = group;
        }
    }

    // for every created cell, compute their subcells
    for (const auto& pair : created_cells)
    {
        size_t id = pair.second;
        _cells[id].first_child_id = _cells.size();

        std::vector<const data::GpuResourceBucket*> empty_bucket_set = {};

        _recursive_compute_cell_children(
            id, (_cells[id].group) ? _cells[id].group->buckets : empty_bucket_set,
            (_cells[id].reference_group) ? _cells[id].reference_group->buckets : empty_bucket_set,
            grouping_functions.subspan(1));
    }
}

void GpuSnapshotComparator::_sort_cells(const ImGuiTableSortSpecs* sort_specs)
{
    if (_cells.empty()) return;

    auto compare = [&](const Cell& a, const Cell& b)
    {
        size_t spec_count = (sort_specs) ? sort_specs->SpecsCount : 0;
        for (size_t n = 0; n < spec_count; ++n)
        {
            const auto& spec = sort_specs->Specs[n];

            double delta = 0.0;
            switch (spec.ColumnUserID)
            {
                case (ImGuiID)ColumnId::Name: delta = strcmp(a.key.c_str(), b.key.c_str()); break;

                case (ImGuiID)ColumnId::Size:
                    if (a.group || b.group)
                    {
                        // We use int64_ts here because the substractions will go into the negatives
                        int64_t a_group_size = (a.group) ? a.group->total_size : 0;
                        int64_t b_group_size = (b.group) ? b.group->total_size : 0;

                        delta = a_group_size - b_group_size;
                    }
                    break;

                case (ImGuiID)ColumnId::ReferenceSize:
                    if (a.reference_group || b.reference_group)
                    {
                        int64_t a_ref_size =
                            (a.reference_group) ? a.reference_group->total_size : 0;
                        int64_t b_ref_size =
                            (b.reference_group) ? b.reference_group->total_size : 0;

                        delta = a_ref_size - b_ref_size;
                    }
                    break;

                case (ImGuiID)ColumnId::Comparison:
                    if (a.group || b.group || a.reference_group || b.reference_group)
                    {
                        int64_t a_group_size = (a.group) ? a.group->total_size : 0;
                        int64_t a_ref_size =
                            (a.reference_group) ? a.reference_group->total_size : 0;

                        int64_t b_group_size = (b.group) ? b.group->total_size : 0;
                        int64_t b_ref_size =
                            (b.reference_group) ? b.reference_group->total_size : 0;

                        delta = (a_group_size - a_ref_size) - (b_group_size - b_ref_size);
                    }
                    break;
            }

            if (delta != 0.0)
            {
                return (spec.SortDirection == ImGuiSortDirection_Ascending) ? delta < 0 : delta > 0;
            }
        }

        // If unable to differentiate, use the name string comparison
        return a.key < b.key;
    };

    std::function<void(size_t, size_t)> recursive_sort = [&](size_t from, size_t to)
    {
        auto sort_begin = _cells.begin() + from;
        auto sort_end = _cells.begin() + to;
        std::stable_sort(sort_begin, sort_end, compare);

        for (size_t i = from; i < to; ++i)
        {
            const auto& cell = _cells.at(i);
            if (cell.child_count > 0)
            {
                recursive_sort(cell.first_child_id, cell.first_child_id + cell.child_count);
            }
        }
    };

    const size_t first_id = _cells.front().first_child_id;
    recursive_sort(first_id, first_id + _cells.front().child_count);
}

void GpuSnapshotComparator::_draw_header(
    lm::dvec2 position,
    std::span<const data::GpuResourceSnapshot> snapshots)
{
    const auto& snapshot = snapshots[_snapshot_index];

    ImGui::SetCursorScreenPos(position);

    std::string display = gpu_snapshot_to_string(snapshot);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Selected: %s", display.c_str());
    ImGui::SameLine(300.0f);
    bool select_snapshot = ImGui::Button("Set snapshot...");

    ImGui::SameLine(510.0f);
    ImGui::Checkbox("Darken nested rows", &_darken_rows);

    ImGui::AlignTextToFramePadding();
    if (_reference_index)
    {
        const auto& ref_snapshot = snapshots[*_reference_index];
        display = gpu_snapshot_to_string(ref_snapshot);
        ImGui::Text("Reference: %s", display.c_str());
    }
    else
    {
        ImGui::TextDisabled("No reference for comparison.");
        ImGui::SameLine();
        help_marker("Select another snapshot as a reference to use for the comparison.");
    }
    ImGui::SameLine(300.0f);
    bool select_reference = ImGui::Button("Set reference...");

    if (_reference_index)
    {
        ImGui::SameLine(400.0f);
        if (ImGui::Button("Swap"))
        {
            auto tmp = *_reference_index;
            _reference_index = _snapshot_index;
            _snapshot_index = tmp;

            _reset_cells = true;
        }

        ImGui::SameLine(510.0f);
        ImGui::Checkbox("Hide rows corresponding to identical resources", &_hide_identical);
    }

    hrz::flat_hash_set<size_t> disabled_snapshots;
    if (_reference_index) disabled_snapshots.insert(*_reference_index);

    auto new_left_snapshot = array_selector::array_selector_modal<data::GpuResourceSnapshot>(
        "Select a snapshot to examine", select_snapshot, snapshots,
        [](const auto& snapshot) { return gpu_snapshot_to_string(snapshot); }, _snapshot_index,
        disabled_snapshots);

    disabled_snapshots.clear();
    disabled_snapshots.insert(_snapshot_index);

    auto new_right_snapshot = array_selector::array_selector_modal<data::GpuResourceSnapshot>(
        "Select a snapshot to use as reference", select_reference, snapshots,
        [](const auto& snapshot) { return gpu_snapshot_to_string(snapshot); }, _reference_index,
        disabled_snapshots);

    if (new_left_snapshot)
    {
        _snapshot_index = *new_left_snapshot;
        _reset_cells = true;
    }
    if (new_right_snapshot)
    {
        _reference_index = *new_right_snapshot;
        _reset_cells = true;
    }
}

bool GpuSnapshotComparator::_setup_table() const
{
    ImGuiTableFlags flags = ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable
        | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;

    bool in_table = ImGui::BeginTable("Gpu resources table", 4, flags);
    if (in_table)
    {
        std::string size_display = fmt::format("Size (Snapshot #{})", _snapshot_index);
        std::string reference_display = "Reference (Not set)";
        if (_reference_index)
        {
            reference_display = fmt::format("Reference (Snapshot #{})", *_reference_index);
        }

        ImGui::TableSetupColumn(
            "Resources", ImGuiTableColumnFlags_WidthStretch, 0.0f, (ImGuiID)ColumnId::Name);
        ImGui::TableSetupColumn(
            size_display.c_str(), ImGuiTableColumnFlags_DefaultSort, 160.0f,
            (ImGuiID)ColumnId::Size);
        ImGui::TableSetupColumn("Comparison", 0, 110.0f, (ImGuiID)ColumnId::Comparison);
        ImGui::TableSetupColumn(
            reference_display.c_str(), 0, 160.0f, (ImGuiID)ColumnId::ReferenceSize);

        ImGui::TableSetupScrollFreeze(0, 2);
        ImGui::TableHeadersRow();
    }

    return in_table;
}

void GpuSnapshotComparator::_recursive_cell_tree(size_t id)
{
    const auto& cell = _cells.at(id);

    const bool compare = _reference_index.has_value();

    if (_hide_identical && cell.group && cell.reference_group)
    {
        if (cell.group->total_size == cell.reference_group->total_size) return;
    }

    static constexpr float ROW_HUE_BIGGER = 0.f;
    static constexpr float ROW_HUE_SMALLER = 108.f / 360.f;
    static constexpr float ROW_HUE_NEW = 31.f / 360.f;
    static constexpr float ROW_HUE_REMOVED = 198.f / 360.f;

    static constexpr float saturation = 0.5f;
    static constexpr float value = 0.7f;
    static constexpr float alpha = 0.5f;

    uint32_t row_color = ImGui::GetColorU32(ImGuiCol_WindowBg);

    if (compare)
    {
        if (cell.group && cell.reference_group)
        {
            if (cell.group->total_size > cell.reference_group->total_size)
            {
                row_color = color::from_hsv(ROW_HUE_BIGGER, saturation, value, alpha);
            }
            else if (cell.group->total_size < cell.reference_group->total_size)
            {
                row_color = color::from_hsv(ROW_HUE_SMALLER, saturation, value, alpha);
            }
        }
        else if (cell.group && !cell.reference_group)
        {
            row_color = color::from_hsv(ROW_HUE_NEW, saturation, value, alpha);
        }
        else if (!cell.group && cell.reference_group)
        {
            row_color = color::from_hsv(ROW_HUE_REMOVED, saturation, value, alpha);
        }
    }

    if (_darken_rows && cell.depth > 1)
    {
        double light_coeff = (100.0 - (cell.depth - 1) * 4.0) / 100.0;
        row_color = color::multiply(row_color, light_coeff);
    }

    ImGui::TableNextRow();
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, row_color);

    bool is_leaf = (cell.child_count == 0);
    bool default_open = (id == 0);

    bool open = _table_name_column(cell.key.c_str(), is_leaf, default_open);
    _table_group_size_column(cell.group);
    _table_group_comparison_column(cell.group, cell.reference_group, compare);
    _table_group_size_column(cell.reference_group);

    if (open)
    {
        for (size_t i = 0; i < cell.child_count; ++i)
        {
            _recursive_cell_tree(cell.first_child_id + i);
        }

        ImGui::TreePop();
    }
}

} // namespace ui::widget
