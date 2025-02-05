#include "ui/widget/ui_widget_blob_inspector.h"

#include "data.h"
#include "ui/common/ui_common_array_selector.h"
#include "ui/common/ui_common_preset_menu.h"
#include "ui/common/ui_common_sorted_table.h"
#include "ui/ui_colors.h"
#include "ui/ui_context.h"
#include "ui/ui_helpers.h"
#include "ui/ui_view.h"

#include <imgui.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>

#include <cstring>

namespace ui
{
using namespace helpers;
using namespace context;

namespace widget
{
BlobInspector::BlobInspector()
{
    ImGuiTableFlags flags = ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable
        | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX;

    _sorted_table.reset(new sorted_table::SortedTable<data::Blob>("Blob table", flags));

    _sorted_table->push_column(
        {"Blob", 0, 0, [](const data::Blob& blob) { ImGui::Text("%zu", blob.id); },
         [](const data::Blob& a, const data::Blob& b) { return (double)a.id - (double)b.id; }});
    _sorted_table->push_column(
        {"Size", 0, 0,
         [](const data::Blob& blob)
         {
             auto& buffer = static_fmt_memory_buffer();
             format_buffer(buffer, "{}", (MemorySize)blob.size);
             ImGui::Text("%s", buffer.data());
         },
         [](const data::Blob& a, const data::Blob& b) { return (double)a.size - (double)b.size; }});
    _sorted_table->push_column(
        {"Offset", 0, 0, [](const data::Blob& blob) { ImGui::Text("%#09zx", blob.offset); },
         [](const data::Blob& a, const data::Blob& b)
         { return (double)((int64_t)a.offset - (int64_t)b.offset); }});
    _sorted_table->push_column(
        {"Uses", 0, 0, [](const data::Blob& blob) { ImGui::Text("%zu", blob.use_count); },
         [](const data::Blob& a, const data::Blob& b)
         { return (double)a.use_count - (double)b.use_count; }});
    _sorted_table->push_column(
        {"System", 0, 0, [](const data::Blob& blob) { ImGui::Text("%s", blob.system.c_str()); },
         [](const data::Blob& a, const data::Blob& b)
         { return (double)strcmp(a.system.c_str(), b.system.c_str()); }});
    _sorted_table->push_column(
        {"Layer", 0, 0, [](const data::Blob& blob) { ImGui::Text("%s", blob.layer.c_str()); },
         [](const data::Blob& a, const data::Blob& b)
         { return (double)strcmp(a.layer.c_str(), b.layer.c_str()); }});

    _view.set_bounds(Rect({0.0, 0.0}, {1.0, 1.0}));
    _view.set_zoom_bounds(Rect({1.0, 1.0}, {1000.0, 1.0}));
}

void BlobInspector::draw(
    const Rect& area,
    const data::Database& database,
    userdata::Userdata& userdata,
    ActionBus& action_bus)
{
    auto snapshots = database.get_blob_snapshots();
    if (snapshots.empty())
    {
        ImGui::TextDisabled("No blob snapshot has been received yet.");
        return;
    }

    assert(_selected_snapshot < snapshots.size());

    fmt::memory_buffer buffer;

    // Snapshot selection
    {
        ImGui::AlignTextToFramePadding();
        const auto& snapshot = snapshots[_selected_snapshot];

        format_buffer(
            buffer, "Inspecting snapshot #{} at {} - Capacity: {}", _selected_snapshot,
            (Duration)snapshot.timestamp, (MemorySize)snapshot.capacity);
        ImGui::Text("%s", buffer.data());

        if (snapshot.is_malloc_passthrough)
        {
            ImGui::SameLine();
            ImGui::Text("- Malloc passthrough");
        }
    }

    ImGui::SameLine();
    bool open_selector = ImGui::Button("Select snapshot...");

    ImGui::Separator();

    auto naming_function = [](const data::BlobSnapshot& snapshot)
    {
        double percentage =
            (double)snapshot.allocated_blobs_total_size / (double)snapshot.capacity * 100.0;
        return fmt::format(
            "Snapshot #{} [{} - {:.2Lf}%]", snapshot.id,
            (MemorySize)snapshot.allocated_blobs_total_size, percentage);
    };

    auto new_selected_snapshot = array_selector::array_selector_modal<data::BlobSnapshot>(
        "Select a snapshot to inspect", open_selector, snapshots, naming_function,
        _selected_snapshot);

    bool table_data_is_dirty = false;
    if (new_selected_snapshot)
    {
        _selected_snapshot = *new_selected_snapshot;
        table_data_is_dirty = true;
    }

    // Snapshot information
    const auto& snapshot = snapshots[_selected_snapshot];

    format_buffer(
        buffer, "{} blobs allocated ({} empty) - {} allocations pending ({})",
        snapshot.allocated_blobs_count, snapshot.allocated_empty_blobs.size(),
        snapshot.unallocated_blobs.size(), (MemorySize)snapshot.unallocated_blobs_total_size);
    ImGui::Text("%s", buffer.data());

    size_t remaining_capacity = snapshot.capacity - snapshot.allocated_blobs_total_size;
    format_buffer(
        buffer, "{} occupied ({:.2Lf}%) - {} remaining ({:.2Lf}%)",
        (MemorySize)snapshot.allocated_blobs_total_size,
        (double)snapshot.allocated_blobs_total_size / (double)snapshot.capacity * 100.0,
        (MemorySize)remaining_capacity,
        (double)remaining_capacity / (double)snapshot.capacity * 100.0);
    ImGui::Text("%s", buffer.data());

    // Filter
    hrz::flat_hash_set<size_t> filtered_blobs;
    size_t filtered_blobs_size = 0;

    // Blob gauge
    std::optional<size_t> hovered_blob_index = std::nullopt;
    bool gauge_was_double_clicked = false;

    double gauge_min_offset = 0;
    double gauge_max_offset = snapshot.capacity;

    if (!snapshot.is_malloc_passthrough)
    {
        const lm::dvec2 gauge_size = {ImGui::CalcItemWidth(), ImGui::GetFrameHeight()};
        const lm::dvec2 gauge_p0 = ImGui::GetCursorScreenPos();
        const lm::dvec2 gauge_p1 = gauge_p0 + gauge_size;

        std::optional<size_t> hovered_offset = std::nullopt;

        // We use the view to detect the zooming & scrolling input of the gauge.
        if (!_lock_view)
        {
            auto events = _view.catch_events(Rect(gauge_p0, gauge_p1));
            _view.process_events(events);
        }
        else
        {
            ImGui::InvisibleButton("Gauge button", gauge_size);
        }

        _view.process_smoothing();

        gauge_min_offset = std::max(0.0, _view.get_visible_area().p0.x * (double)snapshot.capacity);
        gauge_max_offset = std::min(
            _view.get_visible_area().p1.x * (double)snapshot.capacity, (double)snapshot.capacity);

        if (ImGui::IsItemHovered())
        {
            // Compute which blob is hovered, if any
            auto& io = ImGui::GetIO();
            lm::dvec2 mouse_pos = io.MousePos;

            double progress = clamp((mouse_pos.x - gauge_p0.x) / gauge_size.x, 0.0, 1.0);
            hovered_offset = (size_t)gauge_min_offset
                + (size_t)(progress * (double)(gauge_max_offset - gauge_min_offset));

            // Detect double clicks
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                gauge_was_double_clicked = true;
            }
        }

        auto* draw_list = ImGui::GetWindowDrawList();

        // "Buckets" are adjacent blobs that are less than one pixel wide.
        // They are combined into buckets to draw them in one rect.
        double bucket_starting_offset_px = -1.;
        double bucket_current_offset_px = -1.;
        double bucket_hovered_offset_px = -1.;
        double bucket_selected_offset_px = -1.;

        auto reset_bucket = [&](double offset)
        {
            bucket_starting_offset_px = offset;
            bucket_current_offset_px = offset;
            bucket_hovered_offset_px = -1.;
        };

        auto expand_bucket = [&](double new_offset)
        {
            assert(new_offset >= bucket_current_offset_px);
            bucket_current_offset_px = new_offset;
        };

        auto hover_bucket = [&](double offset) { bucket_hovered_offset_px = offset; };

        auto select_bucket = [&](double offset) { bucket_selected_offset_px = offset; };

        auto draw_bucket = [&]()
        {
            lm::dvec2 stack_p0 = gauge_p0 + lm::dvec2(bucket_starting_offset_px, 0.0);
            lm::dvec2 stack_p1 = gauge_p0 + lm::dvec2(bucket_current_offset_px, gauge_size.y);

            if (stack_p1.x - stack_p0.x < 1.0)
            {
                stack_p1.x = stack_p0.x + 1.0;
            }

            draw_list->AddRectFilled(stack_p0, stack_p1, color_set::MAGENTA.base);

            if (bucket_selected_offset_px >= 0.0)
            {
                const lm::dvec2 hover_p0 = gauge_p0 + lm::dvec2(bucket_selected_offset_px, 0.0);
                const lm::dvec2 hover_p1 =
                    gauge_p0 + lm::dvec2(bucket_selected_offset_px + 1.0, gauge_size.y);

                draw_list->AddRectFilled(hover_p0, hover_p1, color_set::MAGENTA.active);
            }

            if (bucket_hovered_offset_px >= 0.0)
            {
                const lm::dvec2 hover_p0 = gauge_p0 + lm::dvec2(bucket_hovered_offset_px, 0.0);
                const lm::dvec2 hover_p1 =
                    gauge_p0 + lm::dvec2(bucket_hovered_offset_px + 1.0, gauge_size.y);

                draw_list->AddRectFilled(hover_p0, hover_p1, color_set::RED.hovered);
            }
        };

        ImGui::PushClipRect(gauge_p0, gauge_p1, false);

        // Draw blobs in gauge
        for (size_t blob_index = 0; blob_index < snapshot.allocated_blobs.size(); blob_index++)
        {
            const auto& blob = snapshot.allocated_blobs.at(blob_index);

            if (!_pass_filter(blob))
            {
                continue;
            }

            // Build the filtered blob set
            // (Avoids looping again through all the blobs to do it)
            filtered_blobs.insert(blob_index);
            filtered_blobs_size += blob.size;

            if (blob.offset + blob.size < gauge_min_offset || blob.offset > gauge_max_offset)
            {
                continue;
            }

            const double offset_px = (double)(blob.offset - gauge_min_offset)
                / (double)(gauge_max_offset - gauge_min_offset) * gauge_size.x;
            const double size_px =
                (double)blob.size / (double)(gauge_max_offset - gauge_min_offset) * gauge_size.x;

            const bool is_hovered = hovered_offset.has_value()
                && (blob.offset <= *hovered_offset && blob.offset + blob.size >= *hovered_offset);
            if (is_hovered)
            {
                hovered_blob_index = blob_index;
            }

            const bool is_selected = _sorted_table->get_selected_entry().has_value()
                && (_sorted_table->get_selected_entry().value() == blob_index);

            if (size_px > 1.0)
            {
                draw_bucket();
                reset_bucket(-1.0);

                const lm::dvec2 blob_p0 = gauge_p0 + lm::dvec2(offset_px, 0.0);
                const lm::dvec2 blob_p1 = blob_p0 + lm::dvec2(size_px, gauge_size.y);

                const uint32_t blob_color = (is_hovered) ? color_set::RED.hovered
                    : (is_selected)                      ? color_set::RED.active
                                                         : color_set::RED.base;

                draw_list->AddRectFilled(blob_p0, blob_p1, blob_color);
            }
            else
            {
                if (bucket_current_offset_px < 0.0)
                {
                    reset_bucket(offset_px);
                }
                else
                {
                    if (offset_px - bucket_current_offset_px <= 1.0)
                    {
                        expand_bucket(offset_px);
                    }
                    else
                    {
                        draw_bucket();
                        reset_bucket(offset_px);
                    }
                }

                if (is_hovered)
                {
                    hover_bucket(offset_px);
                }
                else if (is_selected)
                {
                    select_bucket(offset_px);
                }
            }
        }

        // Draw bucket if there is an unfinished one
        if (bucket_starting_offset_px >= 0.0)
        {
            draw_bucket();
        }

        ImGui::PopClipRect();

        // Gauge outline
        draw_list->AddRect(gauge_p0, gauge_p1, ImGui::GetColorU32(ImGuiCol_Text));

        // Gauge controls
        ImGui::SameLine();
        if (ImGui::Button("Fit blobs"))
        {
            _view.set_visible_range_x(
                (double)snapshot.min_blob_offset / (double)snapshot.capacity,
                (double)snapshot.max_blob_offset / (double)snapshot.capacity, true);
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset view"))
        {
            _view.set_visible_range_x(0.0, 1.0, true);
        }

        ImGui::SameLine();
        ImGui::Checkbox("Lock view", &_lock_view);

        ImGui::SameLine();
        help_marker(
            "Leave this unchecked to control the blob gauge view with the mouse.\n"
            "(Click & drag to move, scroll the wheel to zoom in or out)");

        // Bounds
        lm::dvec2 text_pos = ImGui::GetCursorScreenPos();
        uint32_t text_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        ImGui::Dummy({1, ImGui::GetTextLineHeight()});

        format_buffer(buffer, "{}", (MemorySize)gauge_min_offset);
        draw_list->AddText(text_pos, text_color, buffer.data());

        text_pos.x = gauge_p1.x;
        format_buffer(buffer, "{}", (MemorySize)gauge_max_offset);
        draw_text_right_aligned(draw_list, text_pos, text_color, buffer.data());
    }

    // Blob tooltip
    if (hovered_blob_index.has_value())
    {
        ImGui::BeginTooltip();

        const auto& hovered_blob = snapshot.allocated_blobs.at(hovered_blob_index.value());

        format_buffer(buffer, "Offset: {:#09x}", hovered_blob.offset);
        ImGui::Text("%s", buffer.data());

        format_buffer(buffer, "Size: {}", (MemorySize)hovered_blob.size);
        ImGui::Text("%s", buffer.data());

        ImGui::Separator();
        ImGui::TextDisabled("Double-click to see table entry");

        ImGui::EndTooltip();
    }

    // Table settings
    ImGui::Separator();

    ImGui::BeginDisabled(snapshot.known_metadata.empty());
    if (ImGui::Button("Set metadata columns...")) ImGui::OpenPopup("Set metadata columns");
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Filter...")) ImGui::OpenPopup("Blob filter");

    size_t filtered_blobs_count = filtered_blobs.size();
    if (filtered_blobs_count != snapshot.allocated_blobs.size())
    {
        format_buffer(
            buffer, "Showing {} / {} blobs - {} ({:.2Lf} % capacity)", filtered_blobs_count,
            snapshot.allocated_blobs.size(), (MemorySize)filtered_blobs_size,
            (double)filtered_blobs_size / (double)snapshot.capacity * 100.0);
        ImGui::SameLine();
        ImGui::Text("%s", buffer.data());
    }

    // Add & remove special columns to display blob metadata to the table
    auto push_table_metadata_column = [&](const std::string& metadata_name)
    {
        _sorted_table->push_column(
            {metadata_name.c_str(), 0, 0,
             [metadata_name](const data::Blob& blob)
             {
                 for (const auto& blob_metadata : blob.metadata)
                 {
                     if (blob_metadata.name == metadata_name)
                     {
                         ImGui::Text("%s", blob_metadata.value.c_str());
                         return;
                     }
                 }
                 ImGui::TextDisabled("----");
             },
             [metadata_name](const data::Blob& a, const data::Blob& b)
             {
                 // @Todo: Compare parsed number
                 const std::string* a_value = nullptr;
                 const std::string* b_value = nullptr;

                 for (const auto& blob_metadata : a.metadata)
                 {
                     if (blob_metadata.name == metadata_name)
                     {
                         a_value = &blob_metadata.value;
                     }
                 }

                 for (const auto& blob_metadata : b.metadata)
                 {
                     if (blob_metadata.name == metadata_name)
                     {
                         b_value = &blob_metadata.value;
                     }
                 }

                 if (a_value && !b_value) return 1.0;
                 if (!a_value && b_value) return -1.0;
                 if (!a_value && !b_value) return 0.0;

                 return (double)strcmp(a_value->c_str(), b_value->c_str());
             }});
        _metadata_columns.insert(metadata_name);
    };

    // Don't call while iterating through _metadata_columns
    auto remove_table_metadata_column = [&](const std::string& metadata_name)
    {
        _sorted_table->erase_column(metadata_name);
        _metadata_columns.erase(metadata_name);
    };

    if (_reset_metadata_columns)
    {
        // Remove all columns for the previous snapshot
        for (const auto& metadata_name : _metadata_columns)
        {
            _sorted_table->erase_column(metadata_name);
        }
        _metadata_columns.clear();

        // Add the "type" metadata column by default
        // (All blobs probably should have a type)
        if (snapshot.known_metadata.contains("type"))
        {
            push_table_metadata_column("type");
        }

        _reset_metadata_columns = false;
    }

    if (ImGui::BeginPopup("Set metadata columns"))
    {
        ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);

        bool has_all_columns = true;
        for (const auto& metadata_name : snapshot.known_metadata)
        {
            bool has_column = _metadata_columns.contains(metadata_name);
            if (!has_column)
            {
                has_all_columns = false;
            }

            if (ImGui::MenuItem(metadata_name.c_str(), "", has_column))
            {
                if (!has_column)
                {
                    push_table_metadata_column(metadata_name);
                }
                else
                {
                    remove_table_metadata_column(metadata_name);
                }
            }
        }

        ImGui::Separator();
        if (!snapshot.known_metadata.empty() && ImGui::MenuItem("All", "", has_all_columns))
        {
            if (!has_all_columns)
            {
                for (const auto& metadata_name : snapshot.known_metadata)
                {
                    if (!_metadata_columns.contains(metadata_name))
                        push_table_metadata_column(metadata_name);
                }
            }
            else
            {
                for (const auto& metadata_name : snapshot.known_metadata)
                {
                    if (_metadata_columns.contains(metadata_name))
                        remove_table_metadata_column(metadata_name);
                }
            }
        }

        ImGui::PopItemFlag();
        ImGui::EndPopup();
    }

    // Filter table entries
    if (_reset_filter)
    {
        _filter.system.clear();
        _filter.layer.clear();
        _filter.size_min = 0;
        _filter.size_max = snapshot.capacity;
        _filter.max_size_max = snapshot.capacity;
        _filter.metadata.clear();

        _reset_filter = false;
    }

    bool open_dummy = true;
    ImGui::SetNextWindowSize({400.0, 0.0});
    if (ImGui::BeginPopupModal(
            "Blob filter", &open_dummy, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar))
    {
        ImGui::PushItemWidth(-100.0);

        // System & layer
        auto show_filter_combo = [](const char* name, ImGuiComboFlags combo_flags,
                                    std::string& target,
                                    const hrz::flat_hash_set<std::string>& options)
        {
            const char* preview = (target.empty()) ? "Any" : target.c_str();

            if (ImGui::BeginCombo(name, preview, combo_flags))
            {
                if (ImGui::Selectable("Any"))
                {
                    target.clear();
                }

                for (const auto& it : options)
                {
                    if (ImGui::Selectable(it.c_str()))
                    {
                        target = it.c_str();
                    }
                }
                ImGui::EndCombo();
            }
        };

        ImGuiComboFlags combo_flags = ImGuiComboFlags_NoArrowButton;

        show_filter_combo("System", combo_flags, _filter.system, snapshot.systems);
        show_filter_combo("Layer", combo_flags, _filter.layer, snapshot.layers);

        // Size constraints
        format_buffer(buffer, "{}", (MemorySize)_filter.size_min);
        size_t min = 0;
        ImGui::SliderScalar(
            "Min size", ImGuiDataType_U64, &_filter.size_min, &min, &_filter.size_max,
            buffer.data(), ImGuiSliderFlags_Logarithmic);

        format_buffer(buffer, "{}", (MemorySize)_filter.size_max);
        ImGui::SliderScalar(
            "Max size", ImGuiDataType_U64, &_filter.size_max, &_filter.size_min,
            &_filter.max_size_max, buffer.data(), ImGuiSliderFlags_Logarithmic);

        ImGui::PopItemWidth();
        ImGui::Separator();

        // Metadata filters
        std::optional<size_t> to_remove = std::nullopt;
        for (size_t i = 0; i < _filter.metadata.size(); ++i)
        {
            auto& criterion = _filter.metadata[i];
            ImGui::PushID(i);

            if (ImGui::CloseButton(ImGui::GetID("##Close button"), ImGui::GetCursorScreenPos()))
            {
                to_remove = i;
            }

            ImGui::Dummy({20.0, 0.0});
            ImGui::SameLine();

            metadata::draw_editable_filter_row(criterion, snapshot.known_metadata);

            ImGui::PopID();
        }

        if (to_remove.has_value())
        {
            _filter.metadata.erase(_filter.metadata.begin() + to_remove.value());
        }

        ImGui::BeginDisabled(snapshot.known_metadata.empty());
        bool open_metadata_popup = ImGui::Button("Add metadata criterion...");
        ImGui::EndDisabled();

        auto metadata_criterion =
            metadata::show_filter_addition_popup(open_metadata_popup, snapshot.known_metadata);
        if (metadata_criterion.has_value())
        {
            _filter.metadata.push_back(metadata_criterion.value());
        }

        // Menu bar
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Reset filter"))
                {
                    _reset_filter = true;
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Copy preset"))
                {
                    ImGui::SetClipboardText(_save_filter_to_json_string().c_str());
                }

                ImGui::SameLine();
                help_marker(
                    "Copies a string representing the filter to the clipboard.\n"
                    "Not suppported on Linux.");

                if (ImGui::MenuItem("Paste preset"))
                {
                    bool success = _load_filter_from_json_string(ImGui::GetClipboardText());
                    if (!success)
                    {
                        ImGui::OpenPopup("JSON parsing error");
                    }
                }

                ImGui::EndMenu();
            }

            auto preset_result =
                preset_menu::preset_system_menu(userdata.blob_filter_presets, false, "Presets");
            switch (preset_result.action)
            {
                case preset_menu::PresetAction::Select:
                    _load_filter_from_json_string(
                        userdata.blob_filter_presets.at(preset_result.preset_name).json);
                    break;

                case preset_menu::PresetAction::Save:
                    userdata.blob_filter_presets[preset_result.preset_name] = {
                        _save_filter_to_json_string()};
                    break;

                case preset_menu::PresetAction::Erase:
                    userdata.blob_filter_presets.erase(preset_result.preset_name);
                    break;

                default:;
            }

            ImGui::EndMenuBar();
        }

        // End of window
        ImGui::Separator();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    // JSON parsing error modal
    open_dummy = true;
    ImGui::SetNextWindowSize({175.0, 0.0});
    if (ImGui::BeginPopupModal("JSON parsing error", &open_dummy, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored((ImColor)color::ERROR, "Invalid JSON!");
        ImGui::Separator();

        if (ImGui::Button("Ok")) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    // Draw the blob table
    if (gauge_was_double_clicked && hovered_blob_index.has_value())
    {
        _sorted_table->select_entry(hovered_blob_index.value());
        _sorted_table->scroll_to_entry(hovered_blob_index.value());
    }

    _sorted_table->draw(snapshot.allocated_blobs, table_data_is_dirty, filtered_blobs);

    // Table entry context menu & metadata window
    if (_sorted_table->get_hovered_entry().has_value()
        && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("##Table entry context menu");
        _context_menu_allocated_blob_index = _sorted_table->get_hovered_entry().value();
    }

    bool open_metadata_popup = false;
    if (ImGui::BeginPopup("##Table entry context menu"))
    {
        const auto& blob = snapshot.allocated_blobs[_context_menu_allocated_blob_index.value()];

        ImGui::BeginDisabled(blob.metadata.empty());
        if (ImGui::MenuItem("Metadata..."))
        {
            // Opening the next popup here would append its ID to the current popup, which would
            // make it impossible to display without also displaying the current one.
            // This is why we want to open the popup after this one is closed.
            open_metadata_popup = true;
        }
        ImGui::EndDisabled();

        ImGui::EndPopup();
    }

    if (open_metadata_popup)
    {
        ImGui::OpenPopup("##Table entry metadata");
        _sorted_table->select_entry(_context_menu_allocated_blob_index.value());
    }

    if (ImGui::BeginPopup("##Table entry metadata"))
    {
        const auto& blob = snapshot.allocated_blobs[_context_menu_allocated_blob_index.value()];

        format_buffer(buffer, "Blob {} - {}", blob.id, (MemorySize)blob.size);
        ImGui::Text("%s", buffer.data());

        ImGui::Separator();
        metadata::draw_metadata_table(blob.metadata);

        ImGui::EndPopup();
    }
}

void BlobInspector::set_blob_snapshot_index(size_t index)
{
    on_database_clear();
    _selected_snapshot = index;
}

void BlobInspector::on_database_clear()
{
    _selected_snapshot = 0;
    _context_menu_allocated_blob_index = std::nullopt;
    _view.set_visible_range_x(0.0, 1.0);
    _reset_filter = true;
    _reset_metadata_columns = true;
}

bool BlobInspector::_pass_filter(const data::Blob& blob) const
{
    if (!_filter.system.empty() && blob.system != _filter.system) return false;
    if (!_filter.layer.empty() && blob.layer != _filter.layer) return false;
    if (blob.size < _filter.size_min || blob.size > _filter.size_max) return false;

    for (const auto& criterion : _filter.metadata)
    {
        for (const auto& blob_metadata : blob.metadata)
        {
            if (!criterion.pass(blob_metadata.name, blob_metadata.value))
            {
                return false;
            }
        }
    }

    return true;
}

std::string BlobInspector::_save_filter_to_json_string() const
{
    rapidjson::Document document;
    document.SetObject();

    rapidjson::Value v_system, v_layer, v_min, v_max;
    v_system.SetString(_filter.system.c_str(), document.GetAllocator());
    v_layer.SetString(_filter.layer.c_str(), document.GetAllocator());
    v_min.SetUint64(_filter.size_min);
    v_max.SetUint64(_filter.size_max);

    document.AddMember("system", v_system, document.GetAllocator());
    document.AddMember("layer", v_layer, document.GetAllocator());
    document.AddMember("size_min", v_min, document.GetAllocator());
    document.AddMember("size_max", v_max, document.GetAllocator());

    rapidjson::Value v_metadata(rapidjson::kArrayType);
    for (const auto& criterion : _filter.metadata)
    {
        criterion.push_json(v_metadata, document);
    }

    document.AddMember("metadata", v_metadata, document.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    return buffer.GetString();
}

bool BlobInspector::_load_filter_from_json_string(std::string_view json)
{
    rapidjson::Document document;
    document.Parse(json.data(), json.size());

    if (document.HasParseError()) return false;
    if (!document.IsObject()) return false;

    auto it = document.FindMember("system");
    if (it != document.MemberEnd() && it->value.IsString())
    {
        _filter.system = it->value.GetString();
    }

    it = document.FindMember("layer");
    if (it != document.MemberEnd() && it->value.IsString())
    {
        _filter.layer = it->value.GetString();
    }

    it = document.FindMember("size_min");
    if (it != document.MemberEnd() && it->value.IsUint64())
    {
        _filter.size_min = it->value.GetUint64();
    }

    it = document.FindMember("size_max");
    if (it != document.MemberEnd() && it->value.IsUint64())
    {
        _filter.size_max = it->value.GetUint64();
    }

    // The filter's maximum maximum size is based on the cureent snapshot, so there may be
    // clamping to do depending on the snapshot that was inspected when the preset was saved.
    _filter.size_max = std::min(_filter.size_max, _filter.max_size_max);
    _filter.size_min = std::min(_filter.size_min, _filter.size_max);

    _filter.metadata.clear();
    it = document.FindMember("metadata");
    if (it != document.MemberEnd() && it->value.IsArray())
    {
        const auto& array = it->value.GetArray();
        for (rapidjson::SizeType i = 0; i < array.Size(); ++i)
        {
            if (array[i].IsArray())
            {
                _filter.metadata.push_back({});
                _filter.metadata.back().load_from_json(array[i]);
            }
        }
    }

    return true;
}
} // namespace widget
} // namespace ui
