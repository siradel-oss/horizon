#include "ui/widget/timeline/ui_widget_timeline_snapshot_graph.h"

#include "ui/ui_colors.h"

namespace
{
static constexpr double MARKER_HALF_SIZE = 8.0;

struct Snapshots
{
    gsl::span<const data::GpuResourceSnapshot> gpu;
    gsl::span<const data::BlobSnapshot> blobs;
};

std::optional<int64_t> get_last_snapshot_timestamp_before(
    int64_t timestamp,
    const Snapshots& snapshots)
{
    std::optional<int64_t> result = std::nullopt;
    int64_t min = 0;
    int64_t max = timestamp;

    auto process = [&](auto span)
    {
        for (const auto& snapshot : span)
        {
            if (snapshot.timestamp > min && snapshot.timestamp < max)
            {
                result = snapshot.timestamp;
                min = result.value();
            }
            else if (snapshot.timestamp >= max)
            {
                break;
            }
        }
    };

    process(snapshots.gpu);
    process(snapshots.blobs);

    return result;
}

std::optional<int64_t> get_first_snapshot_timestamp_after(
    int64_t timestamp,
    const Snapshots& snapshots)
{
    std::optional<int64_t> result = std::nullopt;
    int64_t min = timestamp;
    int64_t max = std::numeric_limits<int64_t>::max();

    auto process = [&](auto span)
    {
        for (const auto& snapshot : span)
        {
            if (snapshot.timestamp > min && snapshot.timestamp < max)
            {
                result = snapshot.timestamp;
                max = result.value();
                break;
            }
        }
    };

    process(snapshots.gpu);
    process(snapshots.blobs);

    return result;
}
} // namespace

namespace ui::widget
{
using namespace helpers;

void SnapshotGraph::draw_header_column_contents(const Rect& area)
{
    auto* draw_list = ImGui::GetWindowDrawList();

    lm::dvec2 text_pos = area.center() - lm::dvec2(0, ImGui::GetTextLineHeightWithSpacing() / 2.0);
    uint32_t text_col = ImGui::GetColorU32(
        (_cached_gpu_snapshot_count == 0) ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    draw_text_centered(
        draw_list, text_pos, text_col, "{} GPU snapshots", _cached_gpu_snapshot_count);

    text_pos.y += ImGui::GetTextLineHeightWithSpacing();
    text_col = ImGui::GetColorU32(
        (_cached_blob_snapshot_count == 0) ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    draw_text_centered(
        draw_list, text_pos, text_col, "{} blob snapshots", _cached_blob_snapshot_count);
}

void SnapshotGraph::draw(
    const view::Layout& layout,
    const data::Database& database,
    view::ViewEvents& view_events,
    context::ActionBus& action_bus)
{
    if (layout.main.size().x < 1.0 || layout.main.size().y < 1.0) return;

    ImGui::SetCursorScreenPos(layout.main.p0);
    ImGui::InvisibleButton("##Snapshot graph body", layout.main.size());
    bool open_context_menu =
        ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    Snapshots snapshots;
    snapshots.gpu = database.get_gpu_snapshots();
    snapshots.blobs = database.get_blob_snapshots();

    _cached_gpu_snapshot_count = snapshots.gpu.size();
    _cached_blob_snapshot_count = snapshots.blobs.size();

    auto& buffer = static_fmt_memory_buffer();
    auto* draw_list = ImGui::GetWindowDrawList();

    auto visible_area = _view.get_visible_area();

    // GPU snapshots
    for (size_t i = 0; i < snapshots.gpu.size(); ++i)
    {
        const auto& gpu_snapshot = snapshots.gpu[i];

        // Ignore updates that are not visible
        if (gpu_snapshot.timestamp < visible_area.p0.x)
        {
            continue;
        }
        else if (gpu_snapshot.timestamp > visible_area.p1.x)
        {
            break;
        }

        ImGui::PushID(i);

        // Draw marker
        lm::dvec2 center = _view.unit_to_px(lm::dvec2(gpu_snapshot.timestamp, .5), layout.main);

        ImGui::SetCursorScreenPos(center - lm::dvec2(MARKER_HALF_SIZE));
        ImGui::InvisibleButton("##Marker", lm::dvec2(MARKER_HALF_SIZE) * 2.0);

        bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (hovered)
        {
            ImGui::BeginTooltip();

            format_buffer(buffer, "at {}", (Duration)gpu_snapshot.timestamp);
            ImGui::Text("GPU snapshot #%zu", i);
            ImGui::Text("%s", buffer.data());

            ImGui::EndTooltip();

            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                action_bus.push_back(context::Action::inspect_gpu_snapshot(i));
            }
        }

        double half_size = (hovered) ? MARKER_HALF_SIZE + 2.0 : MARKER_HALF_SIZE;
        uint32_t fill_color = (hovered) ? ui::color_set::CYAN.hovered : ui::color_set::CYAN.base;
        uint32_t outline_color = ui::color_set::CYAN.active;

        lm::dvec2 p0 = center + lm::dvec2(0.0, -half_size);
        lm::dvec2 p1 = center + lm::dvec2(half_size, 0.0);
        lm::dvec2 p2 = center + lm::dvec2(0.0, half_size);
        lm::dvec2 p3 = center + lm::dvec2(-half_size, 0.0);

        draw_list->AddQuadFilled(p0, p1, p2, p3, fill_color);
        draw_list->AddQuad(p0, p1, p2, p3, outline_color);

        ImGui::PopID();
    }

    // Blob snapshots
    for (size_t i = 0; i < snapshots.blobs.size(); ++i)
    {
        const auto& blob_snapshot = snapshots.blobs[i];

        // Ignore updates that are not visible
        if (blob_snapshot.timestamp < visible_area.p0.x)
        {
            continue;
        }
        else if (blob_snapshot.timestamp > visible_area.p1.x)
        {
            break;
        }

        ImGui::PushID(i);

        // Draw marker
        lm::dvec2 center = _view.unit_to_px(lm::dvec2(blob_snapshot.timestamp, .5), layout.main);

        ImGui::SetCursorScreenPos(center - lm::dvec2(MARKER_HALF_SIZE));
        ImGui::InvisibleButton("##Marker", lm::dvec2(MARKER_HALF_SIZE) * 2.0);

        bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (hovered)
        {
            ImGui::BeginTooltip();

            format_buffer(buffer, "at {}", (Duration)blob_snapshot.timestamp);
            ImGui::Text("Blob snapshot #%zu", i);
            ImGui::Text("%s", buffer.data());

            ImGui::EndTooltip();

            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                action_bus.push_back(context::Action::inspect_blob_snapshot(i));
            }
        }

        double radius = (hovered) ? MARKER_HALF_SIZE + 2.0 : MARKER_HALF_SIZE;
        uint32_t fill_color =
            (hovered) ? ui::color_set::MAGENTA.hovered : ui::color_set::MAGENTA.base;
        uint32_t outline_color = ui::color_set::MAGENTA.active;

        draw_list->AddCircleFilled(center, radius, fill_color);
        draw_list->AddCircle(center, radius, outline_color);

        ImGui::PopID();
    }

    // Context menu
    if (open_context_menu) ImGui::OpenPopup("##Snapshot context menu");
    if (ImGui::BeginPopup("##Snapshot context menu"))
    {
        auto format_gpu_snapshot = [&](fmt::memory_buffer& buffer, size_t index)
        {
            const int64_t timestamp = snapshots.gpu[index].timestamp;
            format_buffer(buffer, "GPU snapshot #{} at {}", index, (Duration)timestamp);
        };

        auto format_blob_snapshot = [&](fmt::memory_buffer& buffer, size_t index)
        {
            const int64_t timestamp = snapshots.blobs[index].timestamp;
            format_buffer(buffer, "Blob snapshot #{} at {}", index, (Duration)timestamp);
        };

        // List all snapshots
        ImGui::BeginDisabled(snapshots.gpu.empty());
        if (ImGui::BeginMenu("GPU snapshots"))
        {
            for (size_t i = 0; i < snapshots.gpu.size(); i++)
            {
                const auto& snapshot = snapshots.gpu[i];
                ImGui::PushID(i);

                format_gpu_snapshot(buffer, i);
                if (ImGui::MenuItem(buffer.data(), nullptr))
                {
                    const double view_half_width = visible_area.size().x / 2.0;
                    view_events.horizontal_focus = {
                        snapshot.timestamp - view_half_width, snapshot.timestamp + view_half_width};
                }

                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(snapshots.blobs.empty());
        if (ImGui::BeginMenu("Blob snapshots"))
        {
            for (size_t i = 0; i < snapshots.blobs.size(); i++)
            {
                const auto& snapshot = snapshots.blobs[i];
                ImGui::PushID(i);

                format_blob_snapshot(buffer, i);
                if (ImGui::MenuItem(buffer.data(), nullptr))
                {
                    const double view_half_width = visible_area.size().x / 2.0;
                    view_events.horizontal_focus = {
                        snapshot.timestamp - view_half_width, snapshot.timestamp + view_half_width};
                }

                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        // Quick scroll to previous / next snapshot
        std::optional<int64_t> previous_timestamp =
            get_last_snapshot_timestamp_before(visible_area.p0.x, snapshots);
        std::optional<int64_t> next_timestamp =
            get_first_snapshot_timestamp_after(visible_area.p1.x, snapshots);

        ImGui::BeginDisabled(!previous_timestamp.has_value());
        if (ImGui::MenuItem("Go to previous snapshot"))
        {
            const double view_half_width = visible_area.size().x / 2.0;
            view_events.horizontal_focus = {
                previous_timestamp.value() - view_half_width,
                previous_timestamp.value() + view_half_width};
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!next_timestamp.has_value());
        if (ImGui::MenuItem("Go to next snapshot"))
        {
            const double view_half_width = visible_area.size().x / 2.0;
            view_events.horizontal_focus = {
                next_timestamp.value() - view_half_width, next_timestamp.value() + view_half_width};
        }
        ImGui::EndDisabled();

        ImGui::EndPopup();
    }
}

} // namespace ui::widget
