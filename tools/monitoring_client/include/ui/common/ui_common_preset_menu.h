#pragma once

#include "ui/ui_colors.h"
#include "ui/ui_helpers.h"
#include "userdata.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <functional>
#include <optional>

namespace preset_menu
{
enum class PresetAction
{
    None,
    Select,
    Save,
    Erase
};

struct PresetMenuResult
{
    PresetAction action;
    std::string preset_name;
};

// Draws a default menu for common preset handling operations : selecting, saving
// or erasing.
// `save_name` and `save_preset` refer to the values given to the system when the
// user tries to save a preset.
// The default entry, when present, is displayed first in the menu and has a name
// that is the empty string.
// Returns the index of the selected preset if one was selected.
template<typename T>
PresetMenuResult preset_system_menu(
    const userdata::PresetMap<T>& map,
    bool has_default,
    const char* menu_name)
{
    PresetMenuResult result = {};
    bool open_save_new_modal = false;

    std::optional<std::string> save_candidate = std::nullopt;
    std::optional<std::string> erase_candidate = std::nullopt;

    if (ImGui::BeginMenu(menu_name))
    {
        if (has_default && ImGui::MenuItem("Default"))
        {
            result.action = PresetAction::Select;
            result.preset_name = "";
        }

        for (const auto& pair : map)
        {
            if (ImGui::MenuItem(pair.first.c_str()))
            {
                result.action = PresetAction::Select;
                result.preset_name = pair.first;
            }
        }

        ImGui::Separator();

        open_save_new_modal = ImGui::MenuItem("Save new preset...");

        if (!map.empty() && ImGui::BeginMenu("Save preset"))
        {
            for (const auto& pair : map)
            {
                if (ImGui::MenuItem(pair.first.c_str()))
                {
                    save_candidate = {pair.first};
                }
            }

            ImGui::EndMenu();
        }

        if (!map.empty() && ImGui::BeginMenu("Erase preset"))
        {
            for (const auto& pair : map)
            {
                if (ImGui::MenuItem(pair.first.c_str()))
                {
                    erase_candidate = {pair.first};
                }
            }

            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    // Save new modal
    {
        thread_local std::string preset_name;
        thread_local bool preset_name_exists;

        if (open_save_new_modal)
        {
            preset_name = "";
            ImGui::OpenPopup("Save new preset");
        }

        bool check_if_ok = open_save_new_modal;

        if (ImGui::BeginPopupModal("Save new preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::PushItemWidth(260.0);
            check_if_ok |= ImGui::InputText("Preset name", &preset_name);

            if (check_if_ok) preset_name_exists = map.count(preset_name) > 0;

            bool name_is_valid = true;

            if (preset_name.empty())
            {
                name_is_valid = false;
                ImGui::TextColored(
                    (ImColor)ui::color::ERROR, "The preset needs a name to be saved.");
            }
            else if (preset_name == "Default" || preset_name == "default")
            {
                name_is_valid = false;
                ImGui::TextColored((ImColor)ui::color::ERROR, "This preset name is reserved.");
            }
            else if (preset_name_exists)
            {
                ImGui::TextColored(
                    (ImColor)ui::color::WARNING,
                    "This name already exists: The preset will be overwritten!");
            }
            else
            {
                ImGui::TextColored((ImColor)ui::color::SUCCESS, "The preset will be created.");
            }

            ImGui::Separator();
            ImGui::BeginDisabled(!name_is_valid);

            if (ImGui::Button("Save"))
            {
                result.action = PresetAction::Save;
                result.preset_name = preset_name;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndDisabled();
            ImGui::SameLine();

            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    // Save modal
    {
        thread_local std::string preset_name;

        if (save_candidate.has_value())
        {
            preset_name = save_candidate.value();
            ImGui::OpenPopup("Save preset");
        }

        if (ImGui::BeginPopupModal("Save preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Overwrite preset \"%s\"?\n\n", preset_name.c_str());

            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                result.action = PresetAction::Save;
                result.preset_name = preset_name;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // Erase modal
    {
        thread_local std::string preset_name;

        if (erase_candidate.has_value())
        {
            preset_name = erase_candidate.value();
            ImGui::OpenPopup("Erase preset");
        }

        if (ImGui::BeginPopupModal("Erase preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Erase preset \"%s\"?\n\n", preset_name.c_str());

            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                result.action = PresetAction::Erase;
                result.preset_name = preset_name;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    return result;
}
} // namespace preset_menu
