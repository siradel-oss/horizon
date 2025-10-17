#pragma once

#include "data.h"
#include "ui/common/ui_common_metadata.h"
#include "ui/ui_context.h"
#include "ui/ui_helpers.h"
#include "ui/ui_view.h"
#include "ui/widget/ui_widget_gpu_helpers.h"
#include "userdata.h"

#include <rapidjson/fwd.h>

namespace ui::widget
{
class Treemap
{
public:
    Treemap();

    void draw(
        const helpers::Rect& area,
        const data::Database&,
        userdata::Userdata& userdata,
        context::ActionBus&);

    void on_database_clear();
    void set_gpu_snapshot_index(size_t index);

private:
    view::View _view;
    gpu_helpers::GpuBucketGroupOrderTabBar _group_tab_bar;

    size_t _gpu_snapshot_index = 0;

    hrz::flat_hash_map<std::string, uint32_t> _resource_colors;

    bool _group_by_system = true;

    struct Cell
    {
        ui::helpers::Rect unit_rect;

        std::string label;
        uint32_t color;
        size_t total_size = 0;
        size_t filtered_size = 0;
        size_t depth = 0;

        size_t parent_id = 0;
        size_t first_child_id = 0;
        size_t child_count = 0;

        std::vector<const data::GpuResourceBucket*> associated_buckets;

        // Metadata is set only if the cell corresponds to a resource instead of a bucket
        // (which will end up split into several resources anyway)
        const std::vector<data::Metadata>* metadata;

        helpers::Rect approximate_global_unit_rect(
            std::span<const Cell>,
            const helpers::Rect& unit_bounds) const;
    };

    std::vector<Cell> _cells;
    std::optional<size_t> _highlighted_cell_id;
    bool _reset_cells = false;

    struct Filter
    {
        std::string type;
        std::string system;
        std::string layer;

        size_t size_min;
        size_t size_max;

        static constexpr size_t RESOURCE_SIZE_MIN = 0;
        static constexpr size_t RESOURCE_SIZE_MAX = 10'737'418'240; // 10 GiB

        std::vector<metadata::Filter> metadata;

        bool pass_bucket(const data::GpuResourceBucket*) const;
        bool pass_resource(const data::GpuResource*) const;

        void reset();

        std::string to_json_string() const;
        bool load_from_json_string(const char* json);
    };

    Filter _filter;

    void _initialize_cells(
        const data::GpuResourceSnapshot& snapshot,
        std::span<const data::GpuResourceBucketGroupingFunction> grouping_functions);

    // For each associated bucket in a cell, creates a new child cell per resource
    // in the bucket.
    // Return the size of the cell in bytes.
    void _split_cell(size_t cell_id);

    // Assign the color of a cell based on its group (whether it represents
    // a system, layer, or type ?)
    void _color_cell_by_group(size_t cell_id);

    // Assign the color of a cell based on the hash of its label.
    void _color_cell_by_label(size_t cell_id);

    void _compute_filtered_sizes(size_t cell_id);

    // For each associated bucket in a cell, creates a new child cell per key
    // that's returned by the grouping function.
    // The buckets associated to the newly created cells are the ones with a
    // matching key returned by the grouping function.
    // Return the size of the cell in bytes.
    void _split_cell(size_t cell_id, const data::GpuResourceBucketGroupingFunction&);

    // Performs the "split" treemap algorithm, to compute the dimensions of
    // the given cells in order to fit them into the given rect.
    void _compute_treemap(std::span<Cell> cells, const ui::helpers::Rect& base_rect);

    void _recursive_draw(size_t cell_id, const helpers::Rect& rect);
    void _recursive_dropdown(size_t cell_id);
    void _cell_context_menu(size_t cell_id, bool open);
    void _cell_metadata_window(size_t cell_id, bool open);

    std::vector<size_t> _build_navigation_bar_cell_path() const;
    void _recursive_navigation_bar(std::span<size_t> ids, lm::dvec2 position);

    void _snapshot_selector(const data::Database&);

    void _filter_selector(const data::GpuResourceSnapshot&, userdata::Userdata&);
    bool _filter_selector_menu_bar(userdata::Userdata&);
    void _filter_selector_parse_json_error_popup(bool open);

    void _tree_viewer();

    void _focus_on_cell(size_t cell_id);

    // Given a rect representing a pixel area where a cell is to be drawn,
    // returns another rect representing a smaller pixel area where the children
    // of this cell can be drawn.
    helpers::Rect _compute_subcell_rect(const helpers::Rect& main_rect) const;

    // Returns a rect which, from a cell's point of view, is its parent's rect
    helpers::Rect _get_cell_unit_bounds() const { return _cells.at(0).unit_rect; };

    size_t _total_size(std::span<const Cell> cells) const;
};

} // namespace ui::widget
