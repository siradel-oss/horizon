#include "ui/widget/ui_widget_sample_inspector.h"

#include <cinttypes>
#include <cstring>
#include <memory>

namespace ui::widget
{

using namespace helpers;

void SampleInspector::focus_on_sample(uint64_t record_hash, data::SampleId sample_id)
{
    _selected_record_hash = record_hash;
    _selected_sample_id = sample_id;

    _different_record_selected = true;
    _set_scroll_to_selected = true;
}

void SampleInspector::on_database_clear()
{
    _selected_record_hash = std::nullopt;
    _different_record_selected = true;
    _sorted_records.clear();
    _sorted_samples.clear();
}

void SampleInspector::draw(
    const Rect& area,
    const data::Database& database,
    context::ActionBus& action_bus)
{
    const auto& sample_system = database.get_samples();

    if (sample_system.sample_count == 0)
    {
        ImGui::TextDisabled("No samples have been received yet.");
        return;
    }

    // Update total durations
    if (sample_system.threads.size() > _thread_total_durations.size())
    {
        _thread_total_durations.resize(sample_system.threads.size());
    }

    for (size_t i = 0; i < sample_system.threads.size(); i++)
    {
        const auto& thread = sample_system.threads[i];
        if (thread.max_timestamp.has_value() && thread.min_timestamp.has_value())
        {
            _thread_total_durations[i] =
                thread.max_timestamp.value() - thread.min_timestamp.value();
        }
        else
        {
            _thread_total_durations[i] = 0;
        }
    }

    // Layout
    const float min_width = ImGui::GetStyle().WindowPadding.x + SEPARATOR_INTERACT_WIDTH;
    const float max_width = ImGui::GetWindowContentRegionWidth() - 1.0F;

    // We need to prevent the separator line from being pushed out of the window, and prevent
    // the table rect from having a 0-width (which means automatic width for ImGui)
    const float adjusted_inspector_width = clamp(_inspector_width, min_width, max_width);

    const Rect table_rect = area.trim(Direction::Right, adjusted_inspector_width);
    ImGui::BeginChild("Left", table_rect.size(), false);
    {
        static ImGuiTextFilter filter;
        filter.Draw("Filter by name", 300.0F);

        ImGui::SameLine();
        ImGui::BeginDisabled(!filter.IsActive());
        if (ImGui::Button("Clear filter")) filter.Clear();
        ImGui::EndDisabled();

        if (_setup_record_table())
        {
            auto* sort_specs = ImGui::TableGetSortSpecs();

            if (_sorted_records.size() < sample_system.records.size())
            {
                // New records have been added, the table has to be sorted
                _sorted_records.clear();
                _extract_sorted_records(sample_system.records, sort_specs);

                if (sort_specs) sort_specs->SpecsDirty = false;
            }
            else if (sort_specs && sort_specs->SpecsDirty)
            {
                // The table sort specs have been edited
                std::stable_sort(
                    _sorted_records.begin(), _sorted_records.end(),
                    [&](uint64_t a, uint64_t b)
                    {
                        return _compare_records(
                            sample_system.records.at(a), sample_system.records.at(b), sort_specs);
                    });

                sort_specs->SpecsDirty = false;
            }

            for (uint64_t record_hash : _sorted_records)
            {
                bool passed_filter =
                    filter.PassFilter(sample_system.records.at(record_hash).name.c_str());

                if (_set_scroll_to_selected && _selected_record_hash.has_value()
                    && _selected_record_hash.value() == record_hash)
                {
                    ImGui::SetScrollHereY();
                    if (!passed_filter)
                    {
                        filter.Clear();
                        passed_filter = true;
                    }
                }

                if (passed_filter) _draw_record_table_row(record_hash, sample_system.records);
            }

            ImGui::EndTable();
        }

        ImGui::EndChild();
    }

    ImGui::SameLine();
    _draw_separator_line();

    ImGui::SameLine();
    ImGui::BeginChild("Right", {0.0, 0.0}, false);
    {
        ImGui::Text("Inspector");
        ImGui::Separator();

        if (_selected_record_hash)
        {
            _draw_inspector(
                *_selected_record_hash, sample_system, database.get_threads(), action_bus);
        }
        else
        {
            ImGui::TextDisabled("Click on a sample in the list to inspect it.");
        }

        ImGui::EndChild();
    }

    // Clear flags
    _different_record_selected = false;
    _set_scroll_to_selected = false;
}

bool SampleInspector::_setup_record_table() const
{
    ImGuiTableFlags flags = ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_Sortable | ImGuiTableFlags_NoBordersInBody
        | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter
        | ImGuiTableFlags_RowBg;

    const auto outer_size = available_rect().size();
    bool in_table = ImGui::BeginTable("Records table", 7, flags, outer_size);

    if (in_table)
    {
        // Prevent the headers row from scrolling
        ImGui::TableSetupScrollFreeze(0, 1);

        ImGui::TableSetupColumn(
            "Sample", ImGuiTableColumnFlags_WidthStretch, 0.0, (ImGuiID)RecordColumnId::SampleName);
        ImGui::TableSetupColumn("Incl. Time", 0, 65.0, (ImGuiID)RecordColumnId::TotalInclusive);
        ImGui::TableSetupColumn("Excl. Time", 0, 65.0, (ImGuiID)RecordColumnId::TotalExclusive);
        ImGui::TableSetupColumn("Incl. %", 0, 60.0, (ImGuiID)RecordColumnId::PercentageInclusive);
        ImGui::TableSetupColumn("Excl. %", 0, 60.0, (ImGuiID)RecordColumnId::PercentageExclusive);
        ImGui::TableSetupColumn("Avg. Incl.", 0, 65.0, (ImGuiID)RecordColumnId::AverageInclusive);
        ImGui::TableSetupColumn("Avg. Excl.", 0, 65.0, (ImGuiID)RecordColumnId::AverageExclusive);
        ImGui::TableHeadersRow();
    }

    return in_table;
}

bool SampleInspector::_setup_sample_table() const
{
    ImGuiTableFlags flags = ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_Sortable | ImGuiTableFlags_NoBordersInBody
        | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter
        | ImGuiTableFlags_RowBg;

    const auto outer_size = available_rect().size();
    bool in_table = ImGui::BeginTable("Samples table", 7, flags, outer_size);

    if (in_table)
    {
        // Prevent the headers row from scrolling
        ImGui::TableSetupScrollFreeze(0, 1);

        ImGui::TableSetupColumn(
            "Called from", ImGuiTableColumnFlags_WidthStretch, 0.0,
            (ImGuiID)SampleColumnId::Parent);
        ImGui::TableSetupColumn("Called at", 0, 60.0, (ImGuiID)SampleColumnId::Timestamp);
        ImGui::TableSetupColumn("Incl. Time", 0, 60.0, (ImGuiID)SampleColumnId::TimeInclusive);
        ImGui::TableSetupColumn("Excl. Time", 0, 60.0, (ImGuiID)SampleColumnId::TimeExclusive);
        ImGui::TableSetupColumn(
            "Incl. %", ImGuiTableColumnFlags_DefaultHide, 55.0,
            (ImGuiID)SampleColumnId::PercentageInclusive);
        ImGui::TableSetupColumn(
            "Excl. %", ImGuiTableColumnFlags_DefaultHide, 55.0,
            (ImGuiID)SampleColumnId::PercentageExclusive);
        ImGui::TableSetupColumn("In thread", 0, 55.0, (ImGuiID)SampleColumnId::Thread);
        ImGui::TableHeadersRow();
    }

    return in_table;
}

void SampleInspector::_draw_record_table_row(
    uint64_t record_hash,
    const hrz::flat_hash_map<uint64_t, data::SampleRecord>& records)
{
    assert(_thread_total_durations.size() > 0);

    const auto& record = records.at(record_hash);

    int64_t main_thread_duration = _thread_total_durations.at(0);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    bool selected = (_selected_record_hash && *_selected_record_hash == record_hash);
    if (ImGui::Selectable(record.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
    {
        _different_record_selected = true;
        _selected_record_hash = (selected) ? std::nullopt : std::make_optional(record_hash);
    }
    ImGui::TableNextColumn();

    auto& buffer = static_fmt_memory_buffer();

    format_buffer(buffer, "{}", (Duration)record.inclusive_time);
    ImGui::Text("%s", buffer.data());
    ImGui::TableNextColumn();

    format_buffer(buffer, "{}", (Duration)record.exclusive_time);
    ImGui::Text("%s", buffer.data());
    ImGui::TableNextColumn();

    double inclusive_ratio = (double)(record.inclusive_time) / (double)main_thread_duration;
    ImGui::Text("%02.2f%%", inclusive_ratio * 100.0);
    ImGui::TableNextColumn();

    double exclusive_ratio = (double)(record.exclusive_time) / (double)main_thread_duration;
    ImGui::Text("%02.2f%%", exclusive_ratio * 100.0);
    ImGui::TableNextColumn();

    format_buffer(buffer, "{}", (Duration)record.average_inclusive_time);
    ImGui::Text("%s", buffer.data());
    ImGui::TableNextColumn();

    format_buffer(buffer, "{}", (Duration)record.average_exclusive_time);
    ImGui::Text("%s", buffer.data());
}

void SampleInspector::_draw_sample_table_row(
    const data::SampleId& id,
    const data::SampleSystem& system,
    std::span<const data::Thread> threads,
    context::ActionBus& action_bus)
{
    const auto& sample = system.get_sample(id);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    const bool is_selected = _selected_sample_id.has_value() && _selected_sample_id.value() == id;
    if (_set_scroll_to_selected && is_selected)
    {
        ImGui::SetScrollHereY();
    }

    const data::Sample* parent = nullptr;
    if (id.local != 0) // root sample doesn't have a parent
    {
        auto parent_id = id;
        parent_id.local = sample.parent_index;
        parent = &system.get_sample(parent_id);
    }

    const char* parent_display = (parent) ? parent->name.c_str() : "---";
    const double parent_incl_time = _sample_parent_duration(id, system);

    ImGui::PushID(id.tree);
    ImGui::PushID(id.local);

    if (ImGui::Selectable(
            parent_display, is_selected,
            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
    {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            _selected_sample_id = id;
            action_bus.push_back(
                context::Action::focus_in_timeline(id, sample.entry - 1, sample.exit + 1));
        }
        else
        {
            _selected_sample_id = (is_selected) ? std::nullopt : std::make_optional(id);
        }
    }
    ImGui::TableNextColumn();

    auto& buffer = static_fmt_memory_buffer();

    format_buffer(buffer, "{}", (Duration)sample.entry);
    ImGui::Text("%s", buffer.data());
    ImGui::TableNextColumn();

    format_buffer(buffer, "{}", (Duration)sample.duration());
    ImGui::Text("%s", buffer.data());
    ImGui::TableNextColumn();

    format_buffer(buffer, "{}", (Duration)sample.exclusive_time());
    ImGui::Text("%s", buffer.data());
    ImGui::TableNextColumn();

    const double incl_ratio = sample.duration() / parent_incl_time;
    ImGui::Text("%02.2f%%", incl_ratio * 100.0);
    ImGui::TableNextColumn();

    const double excl_ratio = sample.exclusive_time() / parent_incl_time;
    ImGui::Text("%02.2f%%", excl_ratio * 100.0);
    ImGui::TableNextColumn();

    const uint32_t thread_id = sample.id.thread;
    const char* thread_name = get_thread_name(thread_id, threads);
    ImGui::Text("%s", thread_name);

    ImGui::PopID();
    ImGui::PopID();
}

void SampleInspector::_draw_inspector(
    uint64_t record_hash,
    const data::SampleSystem& system,
    std::span<const data::Thread> threads,
    context::ActionBus& action_bus)
{
    std::unique_ptr<context::Action> returned_action = nullptr;
    const auto& record = system.records.at(record_hash);

    if (_different_record_selected)
    {
        _sorted_samples.clear();
    }

    ImGui::Text("%s", record.name.c_str());

    if (!record.source_file.empty())
        ImGui::Text("%s - line %" PRIu32, record.source_file.c_str(), record.source_line);

    ImGui::Text("Called %zu times", record.associated_samples.size());
    ImGui::SameLine();
    help_marker("Double click on one of the calls listed below to see it in the Timeline window");

    if (_setup_sample_table())
    {
        auto* sort_specs = ImGui::TableGetSortSpecs();

        if (_sorted_samples.size() < record.associated_samples.size())
        {
            // New samples have been added, the table has to be sorted
            _sort_and_insert_samples(record.associated_samples, system, sort_specs);

            if (sort_specs) sort_specs->SpecsDirty = false;
        }
        else if (sort_specs && sort_specs->SpecsDirty)
        {
            // The table sort specs have been edited
            std::stable_sort(
                _sorted_samples.begin(), _sorted_samples.end(),
                [&](data::SampleId a, data::SampleId b)
                {
                    return _compare_samples(
                        system.get_sample(a), system.get_sample(b), system, sort_specs);
                });

            sort_specs->SpecsDirty = false;
        }

        for (const auto& sample_id : _sorted_samples)
        {
            _draw_sample_table_row(sample_id, system, threads, action_bus);
        }

        ImGui::EndTable();
    }
}

void SampleInspector::_draw_separator_line()
{
    const float line_height = available_rect().size().y;

    lm::dvec2 p0 = ImGui::GetCursorScreenPos();
    p0.x += SEPARATOR_INTERACT_WIDTH / 2.0;

    const lm::dvec2 p1 = p0 + lm::dvec2(0.0, line_height);

    uint32_t line_color = ImGui::GetColorU32(ImGuiCol_Separator);

    ImGui::InvisibleButton("Separator line", {SEPARATOR_INTERACT_WIDTH, line_height});
    if (ImGui::IsItemActive())
    {
        const auto mouse_pos_x = ImGui::GetIO().MousePos.x;
        const auto offset = SEPARATOR_INTERACT_WIDTH / 2.0F + ImGui::GetStyle().WindowPadding.x;

        _inspector_width = std::max(0.0F, ImGui::GetWindowWidth() - mouse_pos_x + offset);

        line_color = ImGui::GetColorU32(ImGuiCol_SeparatorActive);
    }
    else if (ImGui::IsItemHovered())
    {
        line_color = ImGui::GetColorU32(ImGuiCol_SeparatorHovered);
    }

    ImGui::GetWindowDrawList()->AddLine(p0, p1, line_color);
}

void SampleInspector::_extract_sorted_records(
    const hrz::flat_hash_map<uint64_t, data::SampleRecord>& records,
    const ImGuiTableSortSpecs* sort_specs)
{
    for (const auto& pair : records)
    {
        auto record_hash = pair.first;
        _sorted_records.push_back(record_hash);
    }

    const auto eval = [&](uint64_t a, uint64_t b)
    { return _compare_records(records.at(a), records.at(b), sort_specs); };
    std::sort(_sorted_records.begin(), _sorted_records.end(), eval);
}

void SampleInspector::_sort_and_insert_samples(
    std::span<const data::SampleId> ids,
    const data::SampleSystem& system,
    const ImGuiTableSortSpecs* sort_specs)
{
    size_t middle_id = _sorted_samples.size();

    for (size_t i = _sorted_samples.size(); i < ids.size(); ++i)
    {
        _sorted_samples.push_back(ids[i]);
    }

    const auto eval = [&](data::SampleId a, data::SampleId b)
    { return _compare_samples(system.get_sample(a), system.get_sample(b), system, sort_specs); };

    std::sort(_sorted_samples.begin() + middle_id, _sorted_samples.end(), eval);
    std::inplace_merge(
        _sorted_samples.begin(), _sorted_samples.begin() + middle_id, _sorted_samples.end(), eval);
}

bool SampleInspector::_compare_records(
    const data::SampleRecord& a,
    const data::SampleRecord& b,
    const ImGuiTableSortSpecs* sort_specs) const
{
    size_t spec_count = (sort_specs) ? sort_specs->SpecsCount : 0;
    for (size_t n = 0; n < spec_count; ++n)
    {
        const auto& spec = sort_specs->Specs[n];

        double delta = 0.0;
        switch (spec.ColumnUserID)
        {
            case (ImGuiID)RecordColumnId::SampleName:
                delta = strcmp(a.name.c_str(), b.name.c_str());
                break;

            case (ImGuiID)RecordColumnId::TotalInclusive:
            case (ImGuiID)RecordColumnId::PercentageInclusive:
                delta = a.inclusive_time - b.inclusive_time;
                break;

            case (ImGuiID)RecordColumnId::TotalExclusive:
            case (ImGuiID)RecordColumnId::PercentageExclusive:
                delta = a.exclusive_time - b.exclusive_time;
                break;

            case (ImGuiID)RecordColumnId::AverageInclusive:
                delta = a.average_inclusive_time - b.average_inclusive_time;
                break;

            case (ImGuiID)RecordColumnId::AverageExclusive:
                delta = a.average_exclusive_time - b.average_exclusive_time;
                break;

            default: break;
        }

        if (delta != 0.0)
        {
            return (spec.SortDirection == ImGuiSortDirection_Ascending) ? delta < 0 : delta > 0;
        }
    }

    // If none of the above comparisons could establish an order,
    // use the std::string comparison of the names as a fallback
    return a.name < b.name;
}

bool SampleInspector::_compare_samples(
    const data::Sample& a,
    const data::Sample& b,
    const data::SampleSystem& system,
    const ImGuiTableSortSpecs* sort_specs) const
{
    size_t spec_count = (sort_specs) ? sort_specs->SpecsCount : 0;
    for (size_t n = 0; n < spec_count; ++n)
    {
        const auto& spec = sort_specs->Specs[n];

        double delta = 0.0;
        switch (spec.ColumnUserID)
        {
            case (ImGuiID)SampleColumnId::TimeInclusive: delta = a.duration() - b.duration(); break;

            case (ImGuiID)SampleColumnId::TimeExclusive:
                delta = a.exclusive_time() - b.exclusive_time();
                break;

            case (ImGuiID)SampleColumnId::PercentageInclusive:
            {
                const double a_parent_duration = _sample_parent_duration(a.id, system);
                const double b_parent_duration = _sample_parent_duration(b.id, system);
                delta = (a.duration() / a_parent_duration) - (b.duration() / b_parent_duration);
                break;
            }

            case (ImGuiID)SampleColumnId::PercentageExclusive:
            {
                const double a_parent_duration = _sample_parent_duration(a.id, system);
                const double b_parent_duration = _sample_parent_duration(b.id, system);
                delta = (a.exclusive_time() / a_parent_duration)
                    - (b.exclusive_time() / b_parent_duration);
                break;
            }

            case (ImGuiID)SampleColumnId::Timestamp: delta = a.entry - b.entry; break;

            case (ImGuiID)SampleColumnId::Thread:
                delta = (int)a.id.thread - (int)b.id.thread;
                break;

            default: break;
        }

        if (delta != 0.0)
        {
            return (spec.SortDirection == ImGuiSortDirection_Ascending) ? delta < 0 : delta > 0;
        }
    }

    return a.entry < b.entry;
}

double SampleInspector::_sample_parent_duration(
    const data::SampleId& id,
    const data::SampleSystem& system) const
{
    const auto& sample = system.get_sample(id);

    const data::Sample* parent = nullptr;
    if (id.local != 0) // root sample doesn't have a parent
    {
        auto parent_id = id;
        parent_id.local = sample.parent_index;
        parent = &system.get_sample(parent_id);
    }

    return (parent) ? parent->duration() : _thread_total_durations.at(id.thread);
}

} // namespace ui::widget
