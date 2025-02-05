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
    colors[ImGuiCol_Text] = ImVec4(0.16f, 0.20f, 0.21f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.29f, 0.49f, 1.00f, 0.29f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.41f, 0.55f, 0.71f, 0.67f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.88f, 0.91f, 1.00f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.24f, 0.35f, 1.00f, 0.81f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.24f, 0.35f, 1.00f, 0.24f);
    colors[ImGuiCol_Button] = ImVec4(0.24f, 0.35f, 1.00f, 0.38f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.15f, 0.31f, 0.85f, 0.57f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.24f, 0.35f, 1.00f, 0.82f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.62f, 1.00f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.73f, 0.73f, 0.73f, 0.62f);
    colors[ImGuiCol_Tab] = ImVec4(0.83f, 0.91f, 1.00f, 0.93f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.29f, 0.49f, 1.00f, 0.29f);
    colors[ImGuiCol_TabActive] = ImVec4(0.41f, 0.58f, 1.00f, 0.83f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.88f, 0.91f, 1.00f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.70f, 0.76f, 0.93f, 1.00f);
}

const size_t STYLE_COUNT = 4;
const Style STYLES[] = {
    {"Horizon Light", false, &horizon_light_style},
    {"ImGui Light", false, []() { ImGui::StyleColorsLight(); }},
    {"ImGui Dark", true, []() { ImGui::StyleColorsDark(); }},
    {"ImGui Classic", true, []() { ImGui::StyleColorsClassic(); }}};

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
