#pragma once

#include "hrz/fnd/flat_hash_set.h"

#include <imgui.h>

#include <functional>
#include <optional>
#include <span>
#include <string>

namespace array_selector
{

template<typename T>
using NamingFunction = std::function<std::string(const T&)>;

// Draws a modal allowing to select one element among the given span.
// The modal closes when one element has been selected.
template<typename T>
std::optional<size_t> array_selector_modal(
    const char* title,
    bool is_open,
    std::span<const T> elements,
    NamingFunction<T> naming_function,
    std::optional<size_t> selected = std::nullopt,
    const hrz::flat_hash_set<size_t> disabled = {})
{
    if (is_open)
    {
        ImGui::OpenPopup(title);
    }

    std::optional<size_t> result;

    ImGui::SetNextWindowSizeConstraints({350.0, 0.0}, {450.0, ImGui::GetWindowSize().y});
    bool open = true;
    if (ImGui::BeginPopupModal(title, &open, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushItemWidth(450.0F);

        for (size_t i = 0; i < elements.size(); ++i)
        {
            bool is_selected = selected && *selected == i;
            bool is_disabled = (!disabled.empty()) && disabled.contains(i);

            std::string name = naming_function(elements[i]);

            ImGui::PushID(i);
            if (!is_disabled && ImGui::MenuItem(name.c_str(), nullptr, is_selected))
            {
                result = i;
                ImGui::CloseCurrentPopup();
            }
            else if (is_disabled)
            {
                ImGui::TextDisabled("%s", name.c_str());
            }

            ImGui::PopID();
        }

        ImGui::PopItemWidth();
        ImGui::EndPopup();
    }

    return result;
}

} // namespace array_selector
