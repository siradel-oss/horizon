#include "ui/widget/ui_widget_gpu_treemap.h"

#include "ui/common/ui_common_array_selector.h"
#include "ui/common/ui_common_preset_menu.h"
#include "ui/ui_colors.h"
#include "userdata.h"

#include <hrz_fnd_string_utils.h>

#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>

#include <cstdlib>
#include <cstring>
#include <string_view>

namespace ui::widget
{
using namespace helpers;
using namespace gpu_helpers;
using namespace preset_menu;

constexpr size_t Treemap::Filter::RESOURCE_SIZE_MIN;
constexpr size_t Treemap::Filter::RESOURCE_SIZE_MAX;

Treemap::Treemap() : _view({{0.0, 0.0}, {1.0, 1.0}}), _filter({})
{
    _view.set_bounds(Rect({0.0, 0.0}, {1.0, 1.0}));

    _filter.size_min = Filter::RESOURCE_SIZE_MIN;
    _filter.size_max = Filter::RESOURCE_SIZE_MAX;
}

void Treemap::draw(
    const Rect& area,
    const data::Database& database,
    userdata::Userdata& userdata,
    context::ActionBus& action_bus)
{
    if (database.get_gpu_snapshots().empty())
    {
        ImGui::TextDisabled("No GPU snapshot has been received yet.");
        return;
    }

    // Security against out-of-bounds indices (can happen after loading from a file)
    auto snapshot_count = database.get_gpu_snapshots().size();
    if (_gpu_snapshot_index >= snapshot_count)
    {
        _gpu_snapshot_index = snapshot_count - 1;
        _reset_cells = true;
    }

    const auto& snapshot = database.get_gpu_snapshots()[_gpu_snapshot_index];

    bool changed_tab = _group_tab_bar.draw();
    _reset_cells |= changed_tab;

    ImGui::AlignTextToFramePadding();
    help_marker("Double click on a cell to zoom on it (or right click > \"Focus on cell\")");
    ImGui::SameLine();

    auto cell_path = _build_navigation_bar_cell_path();
    _recursive_navigation_bar(cell_path, ImGui::GetCursorScreenPos());

    ImGui::NewLine();
    _snapshot_selector(database);

    ImGui::SameLine();
    _tree_viewer();

    ImGui::SameLine();
    _filter_selector(snapshot, userdata);

    if (!_cells.empty())
    {
        const auto& root_cell = _cells[0];
        if (root_cell.filtered_size != root_cell.total_size)
        {
            auto& buffer = static_fmt_memory_buffer();
            format_buffer(buffer, "Total size displayed: {}", (MemorySize)root_cell.filtered_size);

            ImGui::SameLine();
            ImGui::Text("%s", buffer.data());
        }
    }

    Rect main_area = available_rect();

    auto layout = view::make_layout(main_area);
    auto events = _view.catch_events(layout.main);
    _view.process_events(events);
    _view.process_smoothing();

    const auto& gpu_snapshot = database.get_gpu_snapshots()[_gpu_snapshot_index];

    if (_reset_cells)
    {
        _reset_cells = false;

        _highlighted_cell_id = std::nullopt;
        _cells.clear();
    }

    // Create cell hierarchy
    const auto& grouping_functions = _group_tab_bar.get_grouping_functions();

    if (_cells.empty()) _initialize_cells(gpu_snapshot, grouping_functions);

    if (_cells.size() <= 1) // The root cell is always created, even when there is no data
    {
        draw_text_centered(
            ImGui::GetWindowDrawList(), layout.main.center(),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), "No resource");
        return;
    }

    const double aspect_ratio = layout.main.size().x / layout.main.size().y;

    // We set the unit rect of the treemap's root cell using the aspect ratio of the
    // drawing area, so that the treemap algorithm can subdivide the cell with the aspect
    // ratio taken into account
    auto& root_cell = _cells[0];
    root_cell.unit_rect = {{0.0, 0.0}, {aspect_ratio, 1.0}};

    // Compute cell rect positions
    _compute_treemap(
        {&_cells[root_cell.first_child_id], root_cell.child_count}, root_cell.unit_rect);

    // We use another view to find where to draw the treemap.
    // This is because drawing views in different rects will only stretch the contents, instead of
    // expanding the view.
    // The easiest solution is to use another view for rendering, where we manually set the
    // visible area based on the state of the persistent `_view`
    Rect warped_rect = _view.get_visible_area().project_into(root_cell.unit_rect, {{0, 0}, {1, 1}});
    view::View warped_view(warped_rect);

    Rect draw_area = warped_view.unit_to_px(root_cell.unit_rect, layout.main);

    ImGui::PushClipRect(layout.main.p0, layout.main.p1, false);
    _recursive_draw(0, draw_area);
    ImGui::PopClipRect();

    return;
}

void Treemap::set_gpu_snapshot_index(size_t index)
{
    _gpu_snapshot_index = index;
    _cells.clear();
}

void Treemap::on_database_clear()
{
    _reset_cells = true;
}

bool Treemap::Filter::pass_bucket(const data::GpuResourceBucket* bucket) const
{
    size_t total_size = bucket->get_total_size();

    if (!type.empty() && bucket->type != type) return false;
    if (!system.empty() && bucket->system != system) return false;
    if (!layer.empty() && bucket->layer != layer) return false;
    if (total_size < size_min || total_size > size_max) return false;

    // As long as the bucket has at least 1 resource with passing all filtering
    // criteria, we allow it, since the individual resources of a bucket will
    // be filtered later.
    for (const auto& resource : bucket->get_resources())
    {
        if (pass_resource(&resource))
        {
            return true;
        }
    }

    return false;
}

bool Treemap::Filter::pass_resource(const data::GpuResource* resource) const
{
    if (resource->size < size_min && resource->size > size_max) return false;
    if (resource->metadata.empty() && metadata.size() > 0) return false;

    for (const auto& criterion : metadata)
    {
        bool passes = false;
        for (const auto& resource_metadata : resource->metadata)
        {
            if (criterion.pass(resource_metadata.name, resource_metadata.value))
            {
                passes = true;
                break;
            }
        }

        if (!passes) return false;
    }

    return true;
}

void Treemap::Filter::reset()
{
    type.clear();
    system.clear();
    layer.clear();
    size_min = RESOURCE_SIZE_MIN;
    size_max = RESOURCE_SIZE_MAX;
    metadata.clear();
}

std::string Treemap::Filter::to_json_string() const
{
    rapidjson::Document document;
    document.SetObject();

    rapidjson::Value v_type, v_system, v_layer, v_min, v_max;
    v_type.SetString(type.c_str(), document.GetAllocator());
    v_system.SetString(system.c_str(), document.GetAllocator());
    v_layer.SetString(layer.c_str(), document.GetAllocator());
    v_min.SetUint64(size_min);
    v_max.SetUint64(size_max);

    document.AddMember("type", v_type, document.GetAllocator());
    document.AddMember("system", v_system, document.GetAllocator());
    document.AddMember("layer", v_layer, document.GetAllocator());
    document.AddMember("size_min", v_min, document.GetAllocator());
    document.AddMember("size_max", v_max, document.GetAllocator());

    rapidjson::Value v_metadata(rapidjson::kArrayType);
    for (const auto& criterion : metadata)
    {
        criterion.push_json(v_metadata, document);
    }

    document.AddMember("metadata", v_metadata, document.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    return buffer.GetString();
}

bool Treemap::Filter::load_from_json_string(const char* json)
{
    rapidjson::Document document;
    document.Parse(json);

    if (document.HasParseError()) return false;
    if (!document.IsObject()) return false;

    auto it = document.FindMember("type");
    if (it != document.MemberEnd() && it->value.IsString())
    {
        type = it->value.GetString();
    }

    it = document.FindMember("system");
    if (it != document.MemberEnd() && it->value.IsString())
    {
        system = it->value.GetString();
    }

    it = document.FindMember("layer");
    if (it != document.MemberEnd() && it->value.IsString())
    {
        layer = it->value.GetString();
    }

    it = document.FindMember("size_min");
    if (it != document.MemberEnd() && it->value.IsUint64())
    {
        size_min = it->value.GetUint64();
    }

    it = document.FindMember("size_max");
    if (it != document.MemberEnd() && it->value.IsUint64())
    {
        size_max = it->value.GetUint64();
    }

    metadata.clear();
    it = document.FindMember("metadata");
    if (it != document.MemberEnd() && it->value.IsArray())
    {
        const auto& array = it->value.GetArray();
        for (rapidjson::SizeType i = 0; i < array.Size(); ++i)
        {
            if (array[i].IsArray())
            {
                metadata.push_back({});
                metadata.back().load_from_json(array[i]);
            }
        }
    }

    return true;
}

void Treemap::_initialize_cells(
    const data::GpuResourceSnapshot& snapshot,
    std::span<const data::GpuResourceBucketGroupingFunction> grouping_functions)
{
    _cells.clear();

    _cells.push_back({});
    auto& root_cell = _cells.back();

    root_cell.total_size = snapshot.get_total_size();
    root_cell.label = "Root";

    for (const auto& bucket : snapshot.get_buckets())
    {
        if (_filter.pass_bucket(&bucket)) root_cell.associated_buckets.push_back(&bucket);
    }

    size_t i = 0;
    for (size_t j = 0; j < grouping_functions.size() - 1; ++j)
    {
        size_t count = _cells.size();
        for (size_t k = i; k < count; ++k)
        {
            _split_cell(k, grouping_functions[j]);
        }

        i = count;
    }

    size_t grouped_cells_end = _cells.size();
    for (size_t j = i; j < _cells.size(); ++j)
    {
        _split_cell(j);
    }

    for (size_t j = 0; j < grouped_cells_end; ++j)
        _color_cell_by_group(j);
    for (size_t j = grouped_cells_end; j < _cells.size(); j++)
        _color_cell_by_label(j);

    _compute_filtered_sizes(0);
}

void Treemap::_split_cell(size_t cell_id)
{
    // Nothing to split
    if (_cells[cell_id].associated_buckets.size() == 0)
    {
        return;
    }

    // The next cells that will be inserted are this cell's children
    _cells[cell_id].first_child_id = _cells.size();

    for (const auto* bucket : _cells[cell_id].associated_buckets)
    {
        Cell* subcell;
        std::string key = bucket->type;

        for (const auto& resource : bucket->get_resources())
        {
            if (!_filter.pass_resource(&resource)) continue;

            _cells.push_back({});
            subcell = &_cells.back();

            subcell->label = key;
            subcell->depth = _cells[cell_id].depth + 1;
            subcell->total_size = resource.size;
            subcell->filtered_size = resource.size;
            subcell->parent_id = cell_id;
            subcell->metadata = &resource.metadata;

            // Update the child indices of the base cell
            _cells[cell_id].child_count++;
        }
    }

    // Sort the subcells by size, so that the the treemap can display them ordered
    auto subcells_begin = _cells.begin() + _cells[cell_id].first_child_id;
    auto subcells_end = subcells_begin + _cells[cell_id].child_count;

    std::sort(
        subcells_begin, subcells_end,
        [&](const Cell& a, const Cell& b) { return a.total_size > b.total_size; });
}

void Treemap::_split_cell(
    size_t cell_id,
    const data::GpuResourceBucketGroupingFunction& grouping_function)
{
    auto bucket_groups =
        data::group_gpu_resource_buckets(_cells[cell_id].associated_buckets, grouping_function);

    // The next cells that will be inserted are this cell's children
    _cells[cell_id].first_child_id = _cells.size();

    for (auto group : bucket_groups)
    {
        _cells.push_back({});
        auto& subcell = _cells.back();

        subcell.label = group.key;
        subcell.associated_buckets = group.buckets;
        subcell.total_size = group.total_size;

        subcell.depth = _cells[cell_id].depth + 1;
        subcell.parent_id = cell_id;

        _cells[cell_id].child_count++;
    }

    // Sort the subcells by size, so that the the treemap can display them ordered
    auto subcells_begin = _cells.begin() + _cells[cell_id].first_child_id;
    auto subcells_end = subcells_begin + _cells[cell_id].child_count;

    std::sort(
        subcells_begin, subcells_end,
        [&](const Cell& a, const Cell& b) { return a.total_size > b.total_size; });
}

void Treemap::_color_cell_by_group(size_t cell_id)
{
    auto& cell = _cells.at(cell_id);

    static const uint32_t SYSTEM_COLOR = color_set::BLUE.base;
    static const uint32_t LAYER_COLOR = color_set::GREEN.base;
    static const uint32_t TYPE_COLOR = color_set::YELLOW.base;

    const bool systems_first = _group_tab_bar.is_grouped_by_systems();
    uint32_t colors[3] = {
        (systems_first) ? SYSTEM_COLOR : LAYER_COLOR, (systems_first) ? LAYER_COLOR : SYSTEM_COLOR,
        TYPE_COLOR};

    // Ignore depth 0 which is root cell (ungrouped)
    size_t color_id = (cell.depth - 1) % IM_ARRAYSIZE(colors);
    cell.color = colors[color_id];
}

void Treemap::_color_cell_by_label(size_t cell_id)
{
    auto& cell = _cells.at(cell_id);
    if (_resource_colors.contains(cell.label))
    {
        cell.color = _resource_colors.at(cell.label);
    }
    else
    {
        cell.color = color::from_string(cell.label, 0.25f, 0.9f, 0.8f);
        _resource_colors[cell.label] = cell.color;
    }
}

void Treemap::_compute_filtered_sizes(size_t cell_id)
{
    auto& cell = _cells.at(cell_id);

    for (size_t i = 0; i < cell.child_count; ++i)
    {
        auto child_id = cell.first_child_id + i;
        _compute_filtered_sizes(child_id);
        cell.filtered_size += _cells[child_id].filtered_size;
    }
}

void Treemap::_compute_treemap(std::span<Cell> cells, const Rect& base_rect)
{
    // Trivial cases
    if (cells.empty())
    {
        return;
    }

    if (cells.size() == 1)
    {
        auto& cell = cells[0];
        auto children = std::span<Cell>(&_cells[cell.first_child_id], cell.child_count);

        cell.unit_rect = base_rect;
        _compute_treemap(children, _get_cell_unit_bounds());

        return;
    }

    // Split the list of cells into two lists L1 and L2
    // Where all indices of cells inside L1 are lower than those of L2
    // And the total size of L1 is as close as L2 as possible
    std::span<Cell> L1, L2;
    Rect R1, R2;

    size_t total_size = _total_size(cells);
    if (total_size == 0)
    {
        return;
    }

    size_t split_size = 0;
    size_t split_index = 0;
    while (split_size <= (total_size / 2) && split_index < cells.size() - 1)
    {
        split_size += cells[split_index].filtered_size;
        split_index++;
    }

    L1 = cells.subspan(0, split_index);
    L2 = cells.subspan(split_index);

    // The split the rect in two halves in a way that the ratio of the areas of
    // the split rects over the area of the main rect is equal to the size ratios
    // of the cell lists
    double ratio_1 = (double)_total_size(L1) / (double)total_size;

    if (base_rect.size().x >= base_rect.size().y)
    {
        double split_width = ratio_1 * base_rect.size().x;
        base_rect.split(Direction::Left, split_width, &R1, &R2);
    }
    else
    {
        double split_height = ratio_1 * base_rect.size().y;
        base_rect.split(Direction::Up, split_height, &R1, &R2);
    }

    // Then apply recursively the algorithm to the sub rects
    _compute_treemap(L1, R1);
    _compute_treemap(L2, R2);
}

void Treemap::_recursive_draw(size_t cell_id, const Rect& rect)
{
    // Special case for root cell: it shouldn't be drawn
    if (cell_id == 0)
    {
        for (size_t i = 0; i < _cells[0].child_count; ++i)
        {
            _recursive_draw(_cells[0].first_child_id + i, rect);
        }
        return;
    }

    const auto& cell = _cells[cell_id];

    const auto px_rect = cell.unit_rect.project_into(rect, _get_cell_unit_bounds());
    const auto child_container_rect = _compute_subcell_rect(px_rect);

    auto* draw_list = ImGui::GetWindowDrawList();

    if ((px_rect.size().x < 1.0 || px_rect.size().y < 1.0)
        && (!_highlighted_cell_id || *_highlighted_cell_id != cell_id))
    {
        return;
    }

    const bool draw_children =
        child_container_rect.size().x > 8.0 && child_container_rect.size().y > 10.0;

    ImGui::PushID(cell_id);

    ImColor fill_color = cell.color;

    // If there this cell has children to draw, we want to make it so that hovering
    // over the area where children are to be drawn does not count as hovering over
    // this cell. Because of how ImGui works, not doing so would result in every
    // overlapping cell being simultaneously marked as hovered
    Rect ignored_rect = {{}, {}};
    if (draw_children && cell.child_count > 0)
    {
        ignored_rect = child_container_rect;
    }

    // Mouse interactions
    ImGui::SetCursorScreenPos(px_rect.p0);
    ImGui::InvisibleButton("##Treemap cell button", px_rect.size(), ImGuiMouseButton_Left);

    bool open_context_menu = false;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
        && !ignored_rect.contains(ImGui::GetIO().MousePos))
    {
        fill_color = color::mix(fill_color, 0xffffffff, 0.1);

        ImGui::BeginTooltip();
        ImGui::Text("%s", cell.label.c_str());

        auto& buffer = static_fmt_memory_buffer();
        format_buffer(buffer, "Size: {}", (MemorySize)cell.total_size);

        ImGui::Text("%s", buffer.data());

        if (cell.filtered_size != cell.total_size)
        {
            format_buffer(buffer, "({} visible)", (MemorySize)cell.filtered_size);
            ImGui::SameLine();
            ImGui::Text("%s", buffer.data());
        }

        if (cell.metadata)
        {
            for (const auto& data : *cell.metadata)
            {
                ImGui::Text("%s: %s", data.name.c_str(), data.value.c_str());
            }
        }

        ImGui::EndTooltip();

        // Focus on double clicked cell
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            _focus_on_cell(cell_id);
            _highlighted_cell_id = cell_id;
        }

        open_context_menu = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    }

    draw_list->AddRectFilled(px_rect.p0, px_rect.p1, fill_color);
    draw_list->AddRect(px_rect.p0, px_rect.p1, color::mix(fill_color, 0xff000000, 0.2));

    // Special highlight
    if (_highlighted_cell_id && *_highlighted_cell_id == cell_id)
    {
        draw_list->AddRect(px_rect.p0, px_rect.p1, color_set::RED.active, 0.0f, 0, 4.0f);
    }

    // Text and stuff
    if (px_rect.size().y > 12.0 && px_rect.size().x > 16.0)
    {
        auto& buffer = static_fmt_memory_buffer();
        format_buffer(buffer, "{}", cell.label.c_str());

        lm::dvec2 text_size = ImGui::CalcTextSize(buffer.data());
        if (text_size.x > px_rect.size().x - 8)
        {
            format_buffer(buffer, "...");
        }

        const lm::dvec2 position(px_rect.center().x, px_rect.p0.y + 7.0);
        const auto color = ImGui::GetColorU32(ImGuiCol_Text);

        draw_text_centered(draw_list, position, color, "{}", buffer.data());
    }

    _cell_context_menu(cell_id, open_context_menu);

    ImGui::PopID();

    // Children
    if (draw_children)
    {
        for (size_t i = cell.first_child_id; i < cell.first_child_id + cell.child_count; ++i)
        {
            _recursive_draw(i, child_container_rect);
        }
    }
}

void Treemap::_recursive_dropdown(size_t cell_id)
{
    const auto& cell = _cells[cell_id];

    auto& buffer = static_fmt_memory_buffer();
    format_buffer(buffer, "{} [{}]", cell.label.c_str(), (MemorySize)cell.total_size);

    ImGui::PushID(cell_id);

    if (cell.child_count == 0)
    {
        if (ImGui::MenuItem(buffer.data()))
        {
            _focus_on_cell(cell_id);
            _highlighted_cell_id = cell_id;
        }
    }

    else
    {
        if (ImGui::BeginMenu(buffer.data()))
        {
            for (size_t i = cell.first_child_id; i < cell.first_child_id + cell.child_count; ++i)
            {
                _recursive_dropdown(i);
            }
            ImGui::EndMenu();
        }
    }

    ImGui::PopID();
}

void Treemap::_cell_context_menu(size_t cell_id, bool open)
{
    if (open) ImGui::OpenPopup("##Treemap cell popup");

    bool open_metadata = false;
    if (ImGui::BeginPopup("##Treemap cell popup"))
    {
        const auto& cell = _cells[cell_id];

        // Simple descriptor
        // We use disabled text to make it easier to differentiate with clickable items
        ImGui::TextDisabled("%s", cell.label.c_str());

        auto& buffer = static_fmt_memory_buffer();
        format_buffer(buffer, "{}", (MemorySize)cell.total_size);

        ImGui::TextDisabled("%s", buffer.data());
        ImGui::Separator();

        // View controls
        if (ImGui::MenuItem("Focus on cell"))
        {
            _focus_on_cell(cell_id);
            _highlighted_cell_id = cell_id;
        }
        if (ImGui::MenuItem("Reset focus"))
        {
            _focus_on_cell(0);
            _highlighted_cell_id = std::nullopt;
        }

        ImGui::Separator();

        // Children visualization
        if (cell.child_count == 0)
        {
            ImGui::TextDisabled("No children");
        }

        else if (ImGui::BeginMenu("Children"))
        {
            for (size_t i = cell.first_child_id; i < cell.first_child_id + cell.child_count; ++i)
            {
                _recursive_dropdown(i);
            }

            ImGui::EndMenu();
        }
        ImGui::Separator();

        // Metadata visualization
        if (!cell.metadata || cell.metadata->empty())
        {
            ImGui::TextDisabled("No metadata");
        }
        else
        {
            open_metadata = ImGui::MenuItem("Metadata...");
        }

        ImGui::EndPopup();
    }

    _cell_metadata_window(cell_id, open_metadata);
}

void Treemap::_cell_metadata_window(size_t cell_id, bool open)
{
    if (open) ImGui::OpenPopup("##Cell metadata");
    if (ImGui::BeginPopup("##Cell metadata"))
    {
        const auto& cell = _cells[cell_id];

        if (!cell.metadata || cell.metadata->empty())
        {
            ImGui::TextDisabled("No metadata associated to this cell.");
        }
        else
        {
            metadata::draw_metadata_table(*cell.metadata);
        }

        ImGui::EndPopup();
    }
}

std::vector<size_t> Treemap::_build_navigation_bar_cell_path() const
{
    std::vector<size_t> cell_path = {};
    if (_highlighted_cell_id)
    {
        // Build a vector containing the path from the root cell id to the highlighted
        // cell id
        cell_path.push_back(*_highlighted_cell_id);
        auto* cell = &_cells[*_highlighted_cell_id];
        while (cell->parent_id != 0)
        {
            cell_path.push_back(cell->parent_id);
            cell = &_cells[cell->parent_id];
        }
    }
    cell_path.push_back(0);
    std::reverse(cell_path.begin(), cell_path.end());

    return cell_path;
}

void Treemap::_recursive_navigation_bar(std::span<size_t> ids, lm::dvec2 position)
{
    if (ids.empty() || _cells.empty())
    {
        return;
    }

    const auto arrow_pos = position + lm::dvec2(0.0, 4.0);
    const auto arrow_col = ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::RenderArrow(ImGui::GetWindowDrawList(), arrow_pos, arrow_col, ImGuiDir_Right);

    position.x += ImGui::GetFontSize() + 4.0;

    const auto id = ids.front();
    const auto& cell = _cells.at(id);
    const char* name = (id == 0) ? "Treemap" : cell.label.c_str();

    // Left click on the button: focus on the corresponding cell
    ImGui::SetCursorScreenPos(position);
    if (ImGui::Button(name))
    {
        _focus_on_cell(id);
        _highlighted_cell_id = (id > 0) ? std::make_optional(id) : std::nullopt;
    }

    // Right click on the button: unselect the cell and select its parent if possible
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        _highlighted_cell_id =
            (cell.parent_id > 0) ? std::make_optional(cell.parent_id) : std::nullopt;
    }

    ImGui::SameLine();
    position.x = ImGui::GetCursorScreenPos().x;
    _recursive_navigation_bar(ids.subspan(1), position);
}

void Treemap::_snapshot_selector(const data::Database& database)
{
    const auto& snapshot = database.get_gpu_snapshots()[_gpu_snapshot_index];

    const auto display_str = gpu_snapshot_to_string(snapshot);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", display_str.c_str());

    ImGui::SameLine();
    bool open_selector = ImGui::Button("Change snapshot...");

    auto next_snapshot = array_selector::array_selector_modal<data::GpuResourceSnapshot>(
        "Select a snapshot to visualize", open_selector, database.get_gpu_snapshots(),
        [](const auto& snapshot) { return gpu_snapshot_to_string(snapshot); }, _gpu_snapshot_index);
    if (next_snapshot)
    {
        _gpu_snapshot_index = *next_snapshot;
        _reset_cells = true;
    }
}

void Treemap::_filter_selector(
    const data::GpuResourceSnapshot& snapshot,
    userdata::Userdata& userdata)
{
    if (ImGui::Button("Filter...")) ImGui::OpenPopup("GPU treemap filter");

    bool open_dummy = true;
    ImGui::SetNextWindowSize({400.0, 0.0});
    if (ImGui::BeginPopupModal(
            "GPU treemap filter", &open_dummy,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar))
    {
        bool filter_changed = false;

        filter_changed |= _filter_selector_menu_bar(userdata);

        ImGui::PushItemWidth(-100.0);

        // Resource type / system / layer selection
        auto show_filter_combo = [&](const char* name, ImGuiComboFlags combo_flags,
                                     std::string& target,
                                     const hrz::flat_hash_set<std::string>& options)
        {
            const char* preview = (target.empty()) ? "Any" : target.c_str();

            if (ImGui::BeginCombo(name, preview, combo_flags))
            {
                if (ImGui::Selectable("Any"))
                {
                    target.clear();
                    filter_changed = true;
                }

                for (const auto& type : options)
                {
                    if (ImGui::Selectable(type.c_str()))
                    {
                        target = type.c_str();
                        filter_changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        };

        ImGuiComboFlags combo_flags = ImGuiComboFlags_NoArrowButton;

        show_filter_combo(
            "Resource type", combo_flags, _filter.type, snapshot.get_known_resource_types());
        show_filter_combo("System", combo_flags, _filter.system, snapshot.get_known_systems());
        show_filter_combo("Layer", combo_flags, _filter.layer, snapshot.get_known_layers());

        ImGui::Separator();

        // Size boundaries range input
        auto& buffer = static_fmt_memory_buffer();
        format_buffer(buffer, "{}", (MemorySize)_filter.size_min);

        filter_changed |= ImGui::SliderScalar(
            "Min size", ImGuiDataType_U64, &_filter.size_min, &Filter::RESOURCE_SIZE_MIN,
            &_filter.size_max, buffer.data(), ImGuiSliderFlags_Logarithmic);

        format_buffer(buffer, "{}", (MemorySize)_filter.size_max);
        filter_changed |= ImGui::SliderScalar(
            "Max size", ImGuiDataType_U64, &_filter.size_max, &_filter.size_min,
            &Filter::RESOURCE_SIZE_MAX, buffer.data(), ImGuiSliderFlags_Logarithmic);

        ImGui::PopItemWidth();
        ImGui::Separator();

        // List of metadata criteria
        std::optional<size_t> to_remove = std::nullopt;
        for (size_t i = 0; i < _filter.metadata.size(); ++i)
        {
            auto& criterion = _filter.metadata[i];
            ImGui::PushID(i);

            // Remove button
            bool remove =
                ImGui::CloseButton(ImGui::GetID("##Close button"), ImGui::GetCursorScreenPos());
            if (remove) to_remove = i;
            ImGui::Dummy(lm::dvec2(20.0, 0.0)); // Advance the cursor
            ImGui::SameLine();

            filter_changed |=
                metadata::draw_editable_filter_row(criterion, snapshot.get_known_metadata());

            ImGui::PopID();
        }

        if (to_remove)
        {
            _filter.metadata.erase(_filter.metadata.begin() + *to_remove);
            filter_changed = true;
        }

        ImGui::BeginDisabled(snapshot.get_known_metadata().empty());
        bool open_metadata_popup = ImGui::Button("Add metadata criterion...");
        ImGui::EndDisabled();

        auto metadata_criterion = metadata::show_filter_addition_popup(
            open_metadata_popup, snapshot.get_known_metadata());
        if (metadata_criterion.has_value())
        {
            _filter.metadata.push_back(metadata_criterion.value());
            filter_changed = true;
        }

        ImGui::Separator();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();

        _reset_cells |= filter_changed;

        ImGui::EndPopup();
    }
}

bool Treemap::_filter_selector_menu_bar(userdata::Userdata& userdata)
{
    bool change = false;
    bool open_parse_json_error_popup = false;

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Reset filter"))
            {
                _filter.reset();
                change = true;
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Copy preset"))
            {
                ImGui::SetClipboardText(_filter.to_json_string().c_str());
            }

            ImGui::SameLine();
            help_marker(
                "Copies a string representing the filter to the clipboard.\n"
                "Not suppported on Linux.");

            if (ImGui::MenuItem("Paste preset"))
            {
                bool success = _filter.load_from_json_string(ImGui::GetClipboardText());
                open_parse_json_error_popup = !success;
            }
            ImGui::EndMenu();
        }

        // Presets menu
        auto& preset_map = userdata.gpu_resource_filter_presets;
        auto menu_result = preset_system_menu(preset_map, false, "Presets");

        switch (menu_result.action)
        {
            case PresetAction::Select:
                _filter.load_from_json_string(preset_map.at(menu_result.preset_name).json.c_str());
                change = true;
                break;

            case PresetAction::Save:
                preset_map[menu_result.preset_name] = {_filter.to_json_string()};
                break;

            case PresetAction::Erase: preset_map.erase(menu_result.preset_name); break;

            default:;
        }

        ImGui::EndMenuBar();
    }

    _filter_selector_parse_json_error_popup(open_parse_json_error_popup);
    return change;
}

void Treemap::_filter_selector_parse_json_error_popup(bool open)
{
    if (open) ImGui::OpenPopup("JSON parsing error");

    bool open_dummy = true;
    ImGui::SetNextWindowSize({175.0, 0.0});
    if (ImGui::BeginPopupModal("JSON parsing error", &open_dummy, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored((ImColor)color::ERROR, "Invalid JSON!");
        ImGui::Separator();

        if (ImGui::Button("Ok")) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

void Treemap::_tree_viewer()
{
    static std::function<void(std::span<const Cell>, size_t)> table_row =
        [&](std::span<const Cell> cells, size_t cell_id)
    {
        ImGui::PushID(cell_id);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        const auto& cell = cells[cell_id];
        const bool leaf = cell.child_count == 0;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;

        bool open = ImGui::TreeNodeEx(cell.label.c_str(), flags);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            _focus_on_cell(cell_id);
            _highlighted_cell_id = (cell_id > 0) ? std::make_optional(cell_id) : std::nullopt;
        }

        ImGui::TableNextColumn();

        auto& buffer = static_fmt_memory_buffer();
        format_buffer(buffer, "{}", (MemorySize)cell.total_size);
        ImGui::Text("%s", buffer.data());

        if (open)
        {
            for (size_t i = 0; i < cell.child_count; ++i)
                table_row(cells, cell.first_child_id + i);

            ImGui::TreePop();
        }
        ImGui::PopID();
    };

    ImGui::BeginDisabled(_cells.empty());
    if (ImGui::Button("View as tree...")) ImGui::OpenPopup("Tree view");
    ImGui::EndDisabled();

    if (_cells.empty()) return;

    ImGui::SetNextWindowSize({400.0, 320.0});
    if (ImGui::BeginPopup("Tree view"))
    {
        if (ImGui::BeginTable("Tree view table", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None, 60.0);
            table_row(_cells, 0);
            ImGui::EndTable();
        }
        ImGui::EndPopup();
    }
}

void Treemap::_focus_on_cell(size_t cell_id)
{
    auto target = _cells[cell_id].approximate_global_unit_rect(_cells, _get_cell_unit_bounds());

    // Since the treemap is computed without taking `_view`'s boundaries into account, we need
    // to normalize the cell's coordinates to be used with the view
    target = target.project_into({{0, 0}, {1, 1}}, _get_cell_unit_bounds());

    _view.set_visible_range_x(target.p0.x, target.p1.x, true);
    _view.set_visible_range_y(target.p0.y, target.p1.y, true);
}

Rect Treemap::_compute_subcell_rect(const Rect& main_rect) const
{
    auto rect = main_rect.trim(4.0);
    rect.p0.y += 12.0;

    return rect;
}

size_t Treemap::_total_size(std::span<const Cell> cells) const
{
    size_t sum = 0;
    for (const auto& cell : cells)
    {
        sum += cell.total_size;
    }
    return sum;
}

Rect Treemap::Cell::approximate_global_unit_rect(
    std::span<const Cell> cells,
    const Rect& unit_bounds) const
{
    // Because the rect into which a cell's children are drawn is computed
    // pixel wise - without taking the view's zoom into account - it is
    // hard to know which is the precise unit area that the view should
    // focus on.
    // This method isn't perfect but still works well.
    auto* parent_cell = this;
    auto target = unit_rect;
    while (parent_cell->parent_id != 0)
    {
        parent_cell = &cells[parent_cell->parent_id];
        target = target.project_into(parent_cell->unit_rect, unit_bounds);
    }

    return target;
}

} // namespace ui::widget
