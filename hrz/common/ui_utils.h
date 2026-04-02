#pragma once

#include "hrz/fnd/flat_hash_set.h"

#include <fmt/core.h>

extern "C"
{
#include <microui/microui.h>
}

#include <vector>

namespace hrz::ui
{

struct StickyPanelState
{
    mu_Container* panel{};
    bool sticked = true;
    int last_body_height = 0;
    int last_content_height = 0;
};

// Creates a scrollable panel that remains full scrolled to the bottom
// if it was already in this state, and new items are appended to its
// content.
void begin_sticky_panel(mu_Context* ctx, StickyPanelState* state, const char* name);
void end_sticky_panel(mu_Context* ctx, StickyPanelState* state);

struct Tooltip
{
    size_t text_start;
    size_t text_length;
    mu_Vec2 position;
    mu_Rect container_rect;
};

struct TooltipContext
{
    std::vector<Tooltip> tooltips;
    fmt::memory_buffer tooltip_buffer;
};

void add_tooltip(mu_Context* ctx, TooltipContext* tooltip_ctx, std::string_view text);
void draw_tooltips(mu_Context* ctx, TooltipContext* tooltip_ctx);

void draw_progress_bar(
    mu_Context* ctx,
    float progress,
    const mu_Color& left_color,
    const mu_Color& right_color,
    TooltipContext* tooltip_ctx = nullptr);

int begin_layout_treenode(
    mu_Context* ctx,
    void* id_data,
    size_t id_size,
    hrz::flat_hash_set<mu_Id>& expanded_nodes);

void end_layout_treenode_header(mu_Context* ctx, bool expanded);
void end_layout_treenode(mu_Context* ctx, bool expanded);

} // namespace hrz::ui
