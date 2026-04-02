#include "ui/widget/timeline/ui_widget_timeline_sample_graph.h"

#include "ui/ui_colors.h"

#include <cinttypes>
#include <cmath>

namespace
{

using namespace ui::helpers;

static constexpr double SAMPLE_HEIGHT = 20.0;
static constexpr double THREAD_SPACING = 4.0;
static constexpr double THREAD_RESIZE_HANDLE_SIZE = 12.0;

// We use a trasparent overlay for the background of odd rows, because we don't want to hide the
// ruler graduations (which are drawn by the timeline, before this widget is drawn).
static constexpr uint32_t ODD_ROW_BG_COLOR = 0x08000000;

struct SampleDrawingContext
{
    const data::SampleSystem* system;

    // The full area available for drawing for the sample graph (pixels)
    Rect main_area;
    // The visible area for the sample graph's current view (us)
    Rect view_area;

    double pixels_per_us;
};

ui::widget::SampleColors _sample_colors_from_fill(ImVec4 fill)
{
    ui::widget::SampleColors colors;

    colors.fill = IM_COL32(fill.x * 255, fill.y * 255, fill.z * 255, 255);
    colors.border = IM_COL32(fill.x * 180, fill.y * 180, fill.z * 180, 255);
    colors.aggregate = IM_COL32(fill.x * 220, fill.y * 220, fill.z * 220, 255);

    return colors;
}

ui::widget::SampleColors _compute_default_sample_colors(const data::Sample& sample)
{
    uint32_t fill = ui::color::from_string(sample.name, 0.5F, 0.9F, 0.65F);
    return _sample_colors_from_fill((ImColor)fill);
}

ui::widget::SampleColors& _get_sample_colors(
    ui::widget::SampleColorMap& colors,
    const data::Sample& sample)
{
    if (!colors.contains(sample.name)) colors[sample.name] = _compute_default_sample_colors(sample);

    return colors.at(sample.name);
}

void _draw_sample_rect(
    const data::Sample& sample,
    const Rect& rect,
    const ui::widget::SampleColors& colors,
    ImDrawList* draw_list)
{
    if (!draw_list)
    {
        draw_list = ImGui::GetWindowDrawList();
    }

    const uint32_t text_color = ImGui::GetColorU32(ImGuiCol_Text);

    draw_list->AddRectFilled(rect.p0, rect.p1, colors.fill);

    // Special aggregation case handling
    if (sample.aggregation_count > 1)
    {
        const float period = rect.size().x / sample.aggregation_count;
        const float ratio =
            (float)((double)sample.aggregation_duration / (double)(sample.exit - sample.entry));
        const float fill = period * ratio;

        if (fill < 1.0)
        {
            draw_list->AddRectFilled(rect.p0, rect.p1, colors.aggregate);
        }
        else
        {
            for (int32_t i = 0; i < sample.aggregation_count; ++i)
            {
                const lm::dvec2 p0 = rect.p0 + lm::dvec2(i * period + fill, 0);
                const lm::dvec2 p1(rect.p0.x + (i + 1) * period, rect.p1.y);
                draw_list->AddRectFilled(p0, p1, colors.aggregate);
            }
        }
    }

    if (rect.size().x >= 3.0)
    {
        draw_list->AddRect(rect.p0, rect.p1, colors.border);

        draw_list->PushClipRect(rect.p0, rect.p1, true);
        draw_list->AddText(
            rect.p0 + lm::vec2(4, (rect.size().y - ImGui::GetFontSize()) / 2.0), text_color,
            sample.name.c_str());
        draw_list->PopClipRect();
    }
}

Rect _compute_sample_rect(
    double entry_us,
    double exit_us,
    size_t depth,
    double thread_offset_y,
    const SampleDrawingContext& ctx)
{
    Rect rect;
    rect.p0.x = ctx.main_area.p0.x + (entry_us - ctx.view_area.p0.x) * ctx.pixels_per_us;
    rect.p1.x = ctx.main_area.p0.x + (exit_us - ctx.view_area.p0.x) * ctx.pixels_per_us;
    rect.p0.y = ctx.main_area.p0.y + thread_offset_y + SAMPLE_HEIGHT * (depth);
    rect.p1.y = ctx.main_area.p0.y + thread_offset_y + SAMPLE_HEIGHT * (depth + 1);

    return rect;
}

void _draw_sample_tooltip(const data::Sample& sample, const data::SampleRecord& record)
{
    ImGui::BeginTooltip();

    auto& buffer = static_fmt_memory_buffer();

    ImGui::Text("%s", sample.name.c_str());

    format_buffer(buffer, "{}", (Duration)sample.duration());
    ImGui::Text("%s", buffer.data());

    if (sample.aggregation_count > 0)
    {
        ImGui::Text("%d consecutive calls", sample.aggregation_count);
    }

    if (!record.source_file.empty())
    {
        ImGui::Separator();
        ImGui::Text("%s", record.source_file.c_str());
        ImGui::Text("Line %" PRIu32, record.source_line);
    }

    ImGui::EndTooltip();
}

struct SampleAction
{
    enum
    {
        FocusOnSample,
        OpenSampleContextMenu
    } type;

    data::SampleId id;
};

std::optional<SampleAction> _handle_sample_rect_interaction(
    const data::Sample& sample,
    const Rect& sample_rect,
    const data::SampleRecord& sample_record)
{
    std::optional<SampleAction> interaction = std::nullopt;

    ImGui::SetCursorScreenPos(sample_rect.p0);
    ImGui::InvisibleButton("##Sample Rect Button", sample_rect.size());

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
    {
        _draw_sample_tooltip(sample, sample_record);

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            interaction = {SampleAction::FocusOnSample, sample.id};
        }
        else if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            interaction = {SampleAction::OpenSampleContextMenu, sample.id};
        }
    }

    return interaction;
}

struct TreeBucket
{
    size_t first_index;
    size_t count;
    double thread_offset_y;
    ui::widget::SampleColors colors;
};

void _add_tree_to_bucket(
    uint32_t thread_id,
    size_t tree_id,
    double thread_offset_y,
    const ui::widget::SampleColors& root_sample_colors,
    std::vector<TreeBucket>& buckets,
    const SampleDrawingContext& ctx)
{
    if (buckets.empty())
    {
        buckets.push_back({});
        buckets.back().first_index = tree_id;
        buckets.back().thread_offset_y = thread_offset_y;
        buckets.back().colors = root_sample_colors;
    }
    else
    {
        const auto& thread = ctx.system->threads.at(thread_id);
        const auto& tree = thread.trees.at(tree_id);

        const size_t pack_last_tree_id = buckets.back().first_index + buckets.back().count - 1;
        assert(tree_id > pack_last_tree_id);

        const double time_diff =
            tree.samples[0].entry - thread.trees[pack_last_tree_id].samples[0].exit;
        const double px_diff = time_diff * ctx.pixels_per_us;

        // We don't want two samples not adjacent to each other to be mixed
        // together in the same rectangle.
        if (tree_id - pack_last_tree_id > 1 || px_diff > 1.0)
        {
            buckets.push_back({});
            buckets.back().first_index = tree_id;
            buckets.back().thread_offset_y = thread_offset_y;
            buckets.back().colors = root_sample_colors;
        }
    }

    buckets.back().count++;
}

void _draw_tree_buckets(
    uint32_t thread_id,
    const std::vector<TreeBucket>& buckets,
    const SampleDrawingContext& ctx)
{
    for (const auto& bucket : buckets)
    {
        const size_t last_index = bucket.first_index + bucket.count - 1;

        const auto& thread = ctx.system->threads.at(thread_id);

        const double from = thread.trees[bucket.first_index].samples[0].entry;
        const double to = thread.trees[last_index].samples[0].exit;

        Rect rect = _compute_sample_rect(from, to, 0, bucket.thread_offset_y, ctx);

        auto* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(rect.p0, rect.p1, bucket.colors.fill);
        draw_list->AddRect(rect.p0, rect.p1, bucket.colors.border);
    }
}

// Returns the sample that was right-clicked if applicable.
std::optional<SampleAction> _draw_thread_samples(
    uint32_t thread_id,
    double thread_offset_y,
    double thread_size_y,
    bool is_odd_row,
    std::vector<TreeBucket>& tree_buckets,
    ui::widget::SampleColorMap& sample_colors,
    const SampleDrawingContext& ctx)
{
    const auto& thread = ctx.system->threads[thread_id];
    std::optional<SampleAction> interaction = std::nullopt;

    if (thread_size_y <= 1.0)
    {
        return interaction;
    }

    Rect thread_area =
        ctx.main_area.subrect(Direction::Up, thread_size_y).offset(0, thread_offset_y);
    thread_area.p1.y = std::min(thread_area.p1.y, ctx.main_area.p1.y);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (is_odd_row)
    {
        const Rect expanded_area = thread_area.trim(Direction::Down, -THREAD_SPACING);
        draw_list->AddRectFilled(expanded_area.p0, expanded_area.p1, ODD_ROW_BG_COLOR);
    }

    if (thread.trees.empty() || thread.min_timestamp > ctx.view_area.p1.x
        || thread.max_timestamp < ctx.view_area.p0.x)
    {
        return interaction;
    }

    const auto tree_range =
        ctx.system->find_trees(ctx.view_area.p0.x, ctx.view_area.p1.x, thread_id);
    if (tree_range.count == 0)
    {
        return interaction;
    }

    ImGui::PushClipRect(thread_area.p0, thread_area.p1, true);

    size_t sample_count = 0;
    tree_buckets.clear();

    for (size_t i = 0; i < tree_range.count; ++i)
    {
        const size_t tree_id = tree_range.first_index + i;
        const auto& tree = ctx.system->threads.at(thread_id).trees.at(tree_id);

        // If the tree's root sample is too small, we want to draw it as a group
        // with other adjacent small trees
        if (tree.samples[0].duration() * ctx.pixels_per_us < 3.0)
        {
            _add_tree_to_bucket(
                thread_id, tree_id, thread_offset_y,
                _get_sample_colors(sample_colors, tree.samples[0]), tree_buckets, ctx);
            continue;
        }

        for (const auto& sample : tree.samples)
        {
            if (sample.exit < ctx.view_area.p0.x || sample.entry > ctx.view_area.p1.x) continue;

            Rect rect =
                _compute_sample_rect(sample.entry, sample.exit, sample.depth, thread_offset_y, ctx);

            if (rect.p0.y > thread_area.p1.y) continue;
            if (rect.size().x < 1.0 || rect.size().y < 1.0) continue;

            ImGui::PushID(sample_count++);

            const auto& colors = _get_sample_colors(sample_colors, sample);
            _draw_sample_rect(sample, rect, colors, draw_list);

            auto sample_interaction = _handle_sample_rect_interaction(
                sample, rect, ctx.system->records.at(sample.record_hash));
            if (sample_interaction.has_value())
            {
                interaction = sample_interaction;
            }

            ImGui::PopID();
        }
    }

    // Draw the packed trees
    _draw_tree_buckets(thread_id, tree_buckets, ctx);

    ImGui::PopClipRect();

    return interaction;
}

void _recursive_draw_sample_as_tree(const data::SampleId& id, const data::SampleSystem& system)
{
    const auto& sample = system.get_sample(id);

    bool in_node = false;

    ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (sample.child_count == 0)
    {
        node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    ImGui::PushID(sample.id.local);
    in_node = ImGui::TreeNodeEx(sample.name.c_str(), node_flags);
    ImGui::PopID();

    if (ImGui::IsItemHovered())
    {
        _draw_sample_tooltip(sample, system.records.at(sample.record_hash));
    }

    if (in_node && sample.child_count > 0)
    {
        for (size_t i = 0; i < sample.child_count; ++i)
        {
            data::SampleId child_id = {id.thread, id.tree, sample.first_child_index + i};
            _recursive_draw_sample_as_tree(child_id, system);
        }

        ImGui::TreePop();
    }
}

void _show_expanded_thread_column_resize_handle(
    const Rect& thread_area,
    ui::widget::SampleGraphThread& thread,
    double& widget_height,
    double opacity)
{
    const char* handle_str_id = "##Thread resize handle";
    if (opacity < 0.01 && ImGui::GetActiveID() != ImGui::GetID(handle_str_id))
    {
        return;
    }

    auto& io = ImGui::GetIO();
    auto* draw_list = ImGui::GetWindowDrawList();

    const Rect handle_rect(thread_area.p1 - lm::dvec2(THREAD_RESIZE_HANDLE_SIZE), thread_area.p1);
    uint32_t grip_color = ImGui::GetColorU32(ImGuiCol_ResizeGrip);

    ImGui::SetCursorScreenPos(handle_rect.p0);
    ImGui::InvisibleButton(handle_str_id, handle_rect.size());

    if (ImGui::IsItemActive())
    {
        grip_color = ImGui::GetColorU32(ImGuiCol_ResizeGripActive);

        double new_height = std::max(
            io.MousePos.y - thread_area.p0.y + handle_rect.size().y / 2.0, 2.0 * SAMPLE_HEIGHT);
        double new_height_clamped = std::round(new_height / SAMPLE_HEIGHT) * SAMPLE_HEIGHT;

        double move = new_height_clamped - thread.target_size;

        thread.target_size += move;
        widget_height += move;
    }

    else if (ImGui::IsItemHovered())
    {
        grip_color = ImGui::GetColorU32(ImGuiCol_ResizeGripHovered);
    }

    const lm::dvec2 grip_p0 = handle_rect.p1;
    const lm::dvec2 grip_p1 = handle_rect.p1 - lm::dvec2(THREAD_RESIZE_HANDLE_SIZE, 0);
    const lm::dvec2 grip_p2 = handle_rect.p1 - lm::dvec2(0, THREAD_RESIZE_HANDLE_SIZE);

    grip_color = ui::color::mix(ui::color::with_alpha(grip_color, 0.0), grip_color, opacity);

    draw_list->AddTriangleFilled(grip_p0, grip_p1, grip_p2, grip_color);
}

bool _is_thread_collapsed(const ui::widget::SampleGraphThread& thread)
{
    return thread.target_size <= SAMPLE_HEIGHT;
}

void _set_thread_collapsed(ui::widget::SampleGraphThread& thread, double& widget_height, bool value)
{
    const double height = (value) ? SAMPLE_HEIGHT : (thread.max_depth + 1) * SAMPLE_HEIGHT;
    const double move = height - thread.target_size;

    thread.target_size += move;
    widget_height += move;
}

void _show_thread_menu_items(
    std::span<ui::widget::SampleGraphThread> threads,
    std::optional<uint32_t> selected,
    double& widget_height)
{
    auto& buffer = static_fmt_memory_buffer();

    const bool thread_collapsed =
        !selected.has_value() || _is_thread_collapsed(threads[selected.value()]);

    const char* collapse_item_name = "Collapse thread";
    if (selected.has_value())
    {
        auto thread_name = threads[selected.value()].name.c_str();
        if (thread_collapsed)
        {
            format_buffer(buffer, "Expand {}", thread_name);
        }
        else
        {
            format_buffer(buffer, "Collapse {}", thread_name);
        }
        collapse_item_name = buffer.data();
    }

    ImGui::BeginDisabled(!selected.has_value());

    if (ImGui::MenuItem(collapse_item_name))
    {
        _set_thread_collapsed(threads[selected.value_or(0)], widget_height, !thread_collapsed);
    }

    ImGui::EndDisabled();

    if (ImGui::MenuItem("Collapse all threads"))
    {
        for (uint32_t i = 0; i < threads.size(); i++)
        {
            _set_thread_collapsed(threads[i], widget_height, true);
        }
    }

    ImGui::Separator();

    bool thread_visible = !selected.has_value() || threads[selected.value()].visible;

    const char* hide_item_name = "Hide thread";
    if (selected.has_value())
    {
        auto thread_name = threads[selected.value()].name.c_str();
        if (thread_visible)
        {
            format_buffer(buffer, "Hide {}", thread_name);
        }
        else
        {
            format_buffer(buffer, "Show {}", thread_name);
        }
        hide_item_name = buffer.data();
    }

    ImGui::BeginDisabled(!selected.has_value());

    if (ImGui::MenuItem(hide_item_name))
    {
        threads[selected.value_or(0)].visible = !thread_visible;
    }

    if (ImGui::MenuItem("Hide other threads"))
    {
        for (uint32_t i = 0; i < threads.size(); i++)
        {
            threads[i].visible = i == selected.value_or(0);
        }
    }

    ImGui::EndDisabled();
    ImGui::Separator();

    if (ImGui::BeginMenu("Visibility"))
    {
        ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);

        bool all_visible = true;
        for (uint32_t i = 0; i < threads.size(); i++)
        {
            if (!threads[i].visible)
            {
                all_visible = false;
            }

            if (ImGui::MenuItem(threads[i].name.c_str(), nullptr, threads[i].visible))
            {
                threads[i].visible = !threads[i].visible;
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("All", nullptr, all_visible))
        {
            for (uint32_t i = 0; i < threads.size(); i++)
            {
                threads[i].visible = !all_visible;
            }
        }

        ImGui::PopItemFlag();
        ImGui::EndMenu();
    }
}

double _interpolate_size(double current, double target)
{
    if (std::abs(current - target) > 0.1)
    {
        current = lerp(current, target, 0.3);
        if (std::abs(current - target) <= 1.0)
        {
            current = target;
        }
    }
    return current;
}

} // namespace

namespace ui::widget
{

using namespace helpers;

void SampleGraph::draw_header_column_contents(const Rect& area)
{
    auto* draw_list = ImGui::GetWindowDrawList();

    ImGui::PushClipRect(area.p0, area.p1, true);

    double thread_offset_y = 0.0;
    size_t drawn_threads = 0;

    for (uint32_t thread_id = 0; thread_id < _threads.size(); thread_id++)
    {
        auto& thread = _threads[thread_id];

        if (!thread.visible) continue;

        const Rect thread_area = area.subrect(Direction::Up, thread.current_size + THREAD_SPACING)
                                     .offset(0.0, thread_offset_y);

        if (thread_offset_y >= area.size().y) break;

        if ((drawn_threads + 1) % 2)
        {
            draw_list->AddRectFilled(thread_area.p0, thread_area.p1, ODD_ROW_BG_COLOR);
        }

        ImGui::PushID(thread_id);

        const bool collapsed = _is_thread_collapsed(thread);
        if (!collapsed)
        {
            const double distance_to_bottom =
                area.p1.y - thread_area.p1.y - THREAD_RESIZE_HANDLE_SIZE;
            const double handle_opacity = clamp(distance_to_bottom / 24.0, 0.0, 1.0);

            _show_expanded_thread_column_resize_handle(
                thread_area, thread, _target_total_size, handle_opacity);
        }

        const lm::dvec2 collapse_button_size = lm::dvec2(ImGui::GetTextLineHeight());
        const lm::dvec2 collapse_button_pos = lm::dvec2(
            thread_area.p1.x - 15.0 - collapse_button_size.x,
            thread_area.p0.y + (SAMPLE_HEIGHT + THREAD_SPACING - collapse_button_size.y) / 2.0);

        // We don't draw anything below the visible area to not create a scrollable area.
        if (_threads.size() > 1 && thread_area.p0.y < area.p1.y)
        {
            ImGui::SetCursorScreenPos(collapse_button_pos);
            if (_threads.size() > 1
                && ImGui::ArrowButtonEx(
                    "##Expand or collapse", (collapsed) ? ImGuiDir_Up : ImGuiDir_Down,
                    collapse_button_size))
            {
                _set_thread_collapsed(thread, _target_total_size, !collapsed);
            }

            const lm::dvec2 close_button_pos = thread_area.p0 + lm::dvec2(4.0);
            if (ImGui::CloseButton(ImGui::GetID("##Hide"), close_button_pos))
            {
                thread.visible = false;
            }
        }

        const lm::dvec2 text_pos = collapse_button_pos - lm::dvec2(6.0, 0.0);
        draw_text_right_aligned(
            draw_list, text_pos, ImGui::GetColorU32(ImGuiCol_Text), "{}", thread.name);

        ImGui::PopID();

        thread_offset_y += thread.current_size + THREAD_SPACING;
        drawn_threads++;
    }

    ImGui::PopClipRect();
}

void SampleGraph::draw(
    const view::Layout& layout,
    const data::Database& database,
    view::ViewEvents& view_events,
    context::ActionBus& action_bus)
{
    if (layout.main.size().x < 1.0 || layout.main.size().y < 1.0) return;

    _current_total_size = _interpolate_size(_current_total_size, _target_total_size);

    const auto& sample_system = database.get_samples();

    if (sample_system.threads.empty())
    {
        return;
    }

    if (_synchronize)
    {
        _handle_automatic_scrolling(sample_system, view_events);
    }

    bool open_view_context_menu = false;
    bool open_sample_context_menu = false;

    ImGui::SetCursorScreenPos(layout.main.p0);
    ImGui::InvisibleButton("##Sample graph body", layout.main.size());
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        open_view_context_menu = true;

        // The thread that was clicked will be set when looping over all threads
        _view_context_menu_thread_id = std::nullopt;
    }

    if (_threads.size() < sample_system.threads.size())
    {
        size_t previous_thread_count = _threads.size();
        _threads.resize(sample_system.threads.size());

        for (size_t i = previous_thread_count; i < _threads.size(); i++)
        {
            _threads[i].current_size = 0;
            _threads[i].target_size = SAMPLE_HEIGHT;
            _threads[i].visible = true;
        }
    }

    for (size_t thread_id = 0; thread_id < sample_system.threads.size(); thread_id++)
    {
        _threads[thread_id].name = get_thread_name(thread_id, database.get_threads());
        _threads[thread_id].max_depth = sample_system.threads[thread_id].max_depth;
        _threads[thread_id].current_size =
            _interpolate_size(_threads[thread_id].current_size, _threads[thread_id].target_size);
    }

    // If there is only one thread, it should always be the size of the widget
    if (_threads.size() == 1)
    {
        _threads.front().current_size = _threads.front().target_size = _current_total_size;
    }

    // Drawing the rectangles
    std::vector<TreeBucket> tree_buckets;
    std::optional<SampleAction> interaction;
    double offset_y = 0;
    size_t drawn_threads = 0;

    SampleDrawingContext ctx;
    ctx.system = &sample_system;
    ctx.main_area = layout.main;
    ctx.view_area = _view.get_visible_area();
    ctx.pixels_per_us = ctx.main_area.size().x / ctx.view_area.size().x;

    for (uint32_t thread_id = 0; thread_id < _threads.size(); thread_id++)
    {
        const auto& thread = _threads[thread_id];

        if (!thread.visible) continue;

        if (offset_y > layout.main.size().y) break;

        auto thread_interaction = _draw_thread_samples(
            thread_id, offset_y, thread.current_size, (drawn_threads + 1) % 2, tree_buckets,
            _sample_colors, ctx);

        if (thread_interaction.has_value())
        {
            assert(!interaction.has_value());
            interaction = thread_interaction;
        }

        const double next_offset_y = offset_y + thread.current_size + THREAD_SPACING;
        if (open_view_context_menu && !_view_context_menu_thread_id.has_value())
        {
            const double mouse_pos_offset_y = ImGui::GetIO().MousePos.y - layout.main.p0.y;
            if (mouse_pos_offset_y >= offset_y && mouse_pos_offset_y < next_offset_y)
            {
                _view_context_menu_thread_id = thread_id;
            }
        }

        offset_y = next_offset_y;
        drawn_threads++;
    }

    if (interaction.has_value())
    {
        const auto& sample = sample_system.get_sample(interaction.value().id);

        switch (interaction.value().type)
        {
            case SampleAction::FocusOnSample:
                view_events.horizontal_focus = {(double)sample.entry, (double)sample.exit};
                break;

            case SampleAction::OpenSampleContextMenu:
                open_sample_context_menu = true;
                open_view_context_menu = false;
                _sample_context_menu_sample_id = sample.id;
                break;
        }
    }

    _sample_context_menu(open_sample_context_menu, sample_system, action_bus);
    _view_context_menu(open_view_context_menu, sample_system, view_events);
}

double SampleGraph::get_preferred_height() const
{
    return _current_total_size;
}

void SampleGraph::on_resize(double size)
{
    _current_total_size = size;
    _target_total_size = size;
}

void SampleGraph::on_database_clear()
{
    _threads.clear();
    _sample_colors.clear();
    _sample_context_menu_sample_id = {};
    _view_context_menu_thread_id = std::nullopt;
}

void SampleGraph::focus_on_sample(data::SampleId sample_id)
{
    const uint32_t thread_id = sample_id.thread;
    if (thread_id < _threads.size())
    {
        _threads[thread_id].visible = true;

        const bool is_root_sample = sample_id.local > 0;
        if (is_root_sample && _is_thread_collapsed(_threads[thread_id]))
        {
            _set_thread_collapsed(_threads[thread_id], _target_total_size, false);
        }
    }
}

void SampleGraph::_view_context_menu(
    bool open,
    const data::SampleSystem& system,
    view::ViewEvents& view_events)
{
    if (open)
    {
        ImGui::OpenPopup("##Body context menu");
    }

    if (ImGui::BeginPopup("##Body context menu"))
    {
        auto visible_area = _view.get_visible_area();
        auto& thread_id = _view_context_menu_thread_id;

        if (ImGui::BeginMenu("Threads", _threads.size() > 1))
        {
            _show_thread_menu_items(_threads, thread_id, _target_total_size);
            ImGui::EndMenu();
        }

        ImGui::Separator();

        ImGui::BeginDisabled(
            !system.min_timestamp.has_value() || visible_area.p0.x < system.min_timestamp.value());
        if (ImGui::MenuItem("Go to timeline start"))
        {
            const double min = system.min_timestamp.value();
            const double range = visible_area.size().x;
            view_events.horizontal_focus = {min - range * 0.25, min + range * 0.75};
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(
            !system.max_timestamp.has_value() || visible_area.p1.x > system.max_timestamp.value());
        if (ImGui::MenuItem("Go to timeline end"))
        {
            const double max = system.max_timestamp.value();
            const double range = visible_area.size().x;
            view_events.horizontal_focus = {max - range * 0.75, max + range * 0.25};
        }
        ImGui::EndDisabled();

        ImGui::MenuItem("Synchronize with Horizon execution", nullptr, &_synchronize);

        ImGui::EndPopup();
    }
}

void SampleGraph::_sample_context_menu(
    bool open,
    const data::SampleSystem& sample_systemm,
    context::ActionBus& action_bus)
{
    if (open)
    {
        ImGui::OpenPopup("##Sample context menu");
    }

    bool open_tree_popup = false;
    bool open_color_popup = false;

    if (ImGui::BeginPopup("##Sample context menu"))
    {
        if (ImGui::BeginMenu("Threads", _threads.size() > 1))
        {
            _show_thread_menu_items(
                _threads, _sample_context_menu_sample_id.thread, _target_total_size);
            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Inspect sample"))
        {
            const auto& sample = sample_systemm.get_sample(_sample_context_menu_sample_id);
            action_bus.push_back(
                context::Action::inspect_sample(
                    sample.record_hash, _sample_context_menu_sample_id));
        }

        open_tree_popup = ImGui::MenuItem("View as tree...");
        open_color_popup = ImGui::MenuItem("Edit color...");

        ImGui::EndPopup();
    }

    _sample_tree_popup(open_tree_popup, sample_systemm, _sample_context_menu_sample_id);
    _sample_color_popup(open_color_popup, sample_systemm, _sample_context_menu_sample_id);
}

void SampleGraph::_sample_tree_popup(
    bool open,
    const data::SampleSystem& sample_systemm,
    const data::SampleId id)
{
    if (open)
    {
        ImGui::OpenPopup("Sample tree tree...");
    }

    ImGui::SetNextWindowSize(lm::dvec2(300, 200));
    if (ImGui::BeginPopup("Sample tree tree..."))
    {
        _recursive_draw_sample_as_tree(id, sample_systemm);
        ImGui::EndPopup();
    }
}

void SampleGraph::_sample_color_popup(
    bool open,
    const data::SampleSystem& sample_systemm,
    const data::SampleId id)
{
    if (open)
    {
        ImGui::OpenPopup("Sample color");
    }

    if (ImGui::BeginPopup("Sample color"))
    {
        const auto& sample = sample_systemm.get_sample(id);
        auto& colors = _sample_colors[sample.name];

        auto fill = ImGui::ColorConvertU32ToFloat4(colors.fill);
        ImGui::ColorPicker3(sample.name.c_str(), (float*)&fill);

        colors = _sample_colors_from_fill(fill);

        if (ImGui::Button("Reset to default")) colors = _compute_default_sample_colors(sample);

        ImGui::EndPopup();
    }
}

void SampleGraph::_handle_automatic_scrolling(
    const data::SampleSystem& system,
    view::ViewEvents& view_events)
{
    if (!system.max_timestamp.has_value())
    {
        return;
    }

    const double max_timestamp = system.max_timestamp.value();

    const auto& visible_area = _view.get_visible_area();

    if (_synchronize && _last_synchronization_timestamp < max_timestamp - .1)
    {
        _last_synchronization_timestamp = max_timestamp;
        if (visible_area.p1.x < max_timestamp)
        {
            auto scroll = (max_timestamp - visible_area.p1.x) + visible_area.size().x * .25;
            view_events.scroll = {lm::dvec2(scroll, 0.0)};
        }
    }
}

} // namespace ui::widget
