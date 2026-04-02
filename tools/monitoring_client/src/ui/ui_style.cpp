#include "ui/ui_style.h"

#include <imgui.h>

struct Style
{
    const char* name;
    bool dark;

    void (*apply)();
};

namespace
{

void horizon_light_style()
{
    ImGui::StyleColorsLight(&ImGui::GetStyle());

    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(0.16F, 0.20F, 0.21F, 1.00F);
    colors[ImGuiCol_PopupBg] = ImVec4(0.94F, 0.94F, 0.94F, 1.00F);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.29F, 0.49F, 1.00F, 0.29F);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.41F, 0.55F, 0.71F, 0.67F);
    colors[ImGuiCol_TitleBg] = ImVec4(0.88F, 0.91F, 1.00F, 1.00F);
    colors[ImGuiCol_TitleBgActive] = ImVec4(1.00F, 1.00F, 1.00F, 1.00F);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00F, 1.00F, 1.00F, 1.00F);
    colors[ImGuiCol_CheckMark] = ImVec4(0.24F, 0.35F, 1.00F, 0.81F);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.24F, 0.35F, 1.00F, 0.24F);
    colors[ImGuiCol_Button] = ImVec4(0.24F, 0.35F, 1.00F, 0.38F);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.15F, 0.31F, 0.85F, 0.57F);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.24F, 0.35F, 1.00F, 0.82F);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30F, 0.62F, 1.00F, 1.00F);
    colors[ImGuiCol_Separator] = ImVec4(0.73F, 0.73F, 0.73F, 0.62F);
    colors[ImGuiCol_Tab] = ImVec4(0.83F, 0.91F, 1.00F, 0.93F);
    colors[ImGuiCol_TabHovered] = ImVec4(0.29F, 0.49F, 1.00F, 0.29F);
    colors[ImGuiCol_TabActive] = ImVec4(0.41F, 0.58F, 1.00F, 0.83F);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.88F, 0.91F, 1.00F, 1.00F);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.70F, 0.76F, 0.93F, 1.00F);
}

const size_t STYLE_COUNT = 4;
const Style STYLES[] = {
    {"Horizon Light", false, &horizon_light_style},
    {"ImGui Light", false, []() { ImGui::StyleColorsLight(); }},
    {"ImGui Dark", true, []() { ImGui::StyleColorsDark(); }},
    {"ImGui Classic", true, []() { ImGui::StyleColorsClassic(); }}
};

const Style* s_current_style = nullptr;

} // namespace

namespace ui::style
{

std::vector<const char*> get_names()
{
    std::vector<const char*> names(STYLE_COUNT);
    for (size_t i = 0; i < STYLE_COUNT; i++)
    {
        names[i] = STYLES[i].name;
    }
    return names;
};

void apply(size_t index)
{
    assert(index < STYLE_COUNT);

    STYLES[index].apply();
    s_current_style = &STYLES[index];
}

bool is_dark_mode()
{
    assert(s_current_style);
    return s_current_style->dark;
}

} // namespace ui::style
