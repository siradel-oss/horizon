#pragma once

#include "data.h"
#include "ui/ui_context.h"
#include "ui/ui_helpers.h"
#include "ui/ui_view.h"
#include "ui/widget/ui_widget_gpu_helpers.h"

namespace ui::widget
{
class GpuSnapshotComparator
{
public:
    void draw(const helpers::Rect& area, const data::Database&, context::ActionBus&);

    void on_database_clear();

private:
    gpu_helpers::GpuBucketGroupOrderTabBar _group_tab_bar;

    size_t _snapshot_index = 0;
    std::optional<size_t> _reference_index = std::nullopt;

    bool _darken_rows = false;
    bool _hide_identical = false;

    struct Cell
    {
        std::string key;
        std::optional<data::GpuResourceBucketGroup> group;
        std::optional<data::GpuResourceBucketGroup> reference_group;

        size_t id;
        size_t first_child_id;
        size_t child_count;
        size_t depth;
    };

    std::vector<Cell> _cells;
    bool _reset_cells = false;

    enum class ColumnId
    {
        Name,
        Size,
        Comparison,
        ReferenceSize
    };

    void _compute_cells(gsl::span<const data::GpuResourceSnapshot> snapshots);
    void _recursive_compute_cell_children(
        size_t parent_cell,
        gsl::span<const data::GpuResourceBucket*> buckets,
        gsl::span<const data::GpuResourceBucket*> reference_buckets,
        gsl::span<const data::GpuResourceBucketGroupingFunction>);

    bool _setup_table() const;
    void _draw_cell_table();
    void _recursive_cell_tree(size_t cell_id);

    void _sort_cells(const ImGuiTableSortSpecs*);

    void _draw_header(lm::dvec2 position, gsl::span<const data::GpuResourceSnapshot>);
};

} // namespace ui::widget
