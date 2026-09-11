// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/ui_utils.h"

#include "hrz/fnd/maths.h"

namespace hrz::ui
{

void begin_sticky_panel(mu_Context* ctx, StickyPanelState* state, const char* name)
{
    mu_begin_panel(ctx, name);
    state->panel = mu_get_current_container(ctx);
}

void end_sticky_panel(mu_Context* ctx, StickyPanelState* state)
{
    mu_end_panel(ctx);

    if (state->panel->body.h <= 0)
    {
        // This can happen if the sticky panel is itself in another
        // scrollable panel, and is below what is currently displayed.
        // In this case, wait until the sticky panel is displayed again
        // before updating its state. (Or it could lose its sticked state.)
        return;
    }

    if (state->panel->content_size.y <= state->panel->body.h)
    {
        // The content is smaller than the panel. Consider it scrolled to
        // the bottom, and hence, sticked.
        state->sticked = true;
    }
    else if (
        state->sticked
        && (state->panel->body.h != state->last_body_height
            || state->panel->content_size.y != state->last_content_height))
    {
        // If the scrolling was sticked, and the sizes changed, scroll to
        // the bottom.
        state->panel->scroll.y = state->panel->content_size.y;
    }
    else
    {
        // Stick the scrolling to the bottom if it's reached.
        //
        // The 10 px difference looks like it comes from the horizontal scroll bar at
        // first glance. But it's still present when the scroll bar is not drawn.
        state->sticked =
            state->panel->scroll.y + state->panel->body.h - 10 >= state->panel->content_size.y;
    }

    state->last_body_height = state->panel->body.h;
    state->last_content_height = state->panel->content_size.y;
}

void add_tooltip(mu_Context* ctx, TooltipContext* tooltip_ctx, std::string_view text)
{
    if (text.empty()) return;

    auto text_start = tooltip_ctx->tooltip_buffer.size();
    tooltip_ctx->tooltip_buffer.append(text);

    tooltip_ctx->tooltips.emplace_back(
        text_start, text.size(), ctx->mouse_pos, mu_get_current_container(ctx)->rect);
}

void draw_tooltips(mu_Context* ctx, TooltipContext* tooltip_ctx)
{
    for (const auto& tooltip : tooltip_ctx->tooltips)
    {
        const char* text = tooltip_ctx->tooltip_buffer.data() + tooltip.text_start;
        const auto text_length = (ptrdiff_t)tooltip.text_length;

        constexpr int interline_height = -1;

        const int line_height = ctx->text_height(ctx->style->font);

        int tooltip_width = 0;
        int tooltip_height = 0;

        const char* line_start = text;
        while (line_start - text < text_length)
        {
            const char* new_line =
                (const char*)memchr(line_start, '\n', text_length - (line_start - text));
            const char* line_end = new_line != nullptr ? new_line : text + text_length;
            const int line_length = line_end - line_start;
            const int line_width = ctx->text_width(ctx->style->font, line_start, line_length);

            tooltip_width = std::max(tooltip_width, line_width);

            if (line_start != text) tooltip_height += interline_height;
            tooltip_height += line_height;

            line_start = line_end + 1;
        }

        constexpr int margin = 10;
        constexpr int padding = 2;
        constexpr int padding_x = padding + 4;
        constexpr int margin_top = padding + 20; // For the title bar

        mu_Rect rect{
            tooltip.position.x - tooltip_width / 2,
            tooltip.position.y - tooltip_height - padding,
            tooltip_width,
            tooltip_height,
        };

        auto cont_rect = tooltip.container_rect;
        if (rect.x < cont_rect.x + padding_x + margin)
        {
            rect.x = cont_rect.x + padding_x + margin;
        }
        if (rect.x > cont_rect.x + cont_rect.w - rect.w - padding_x - margin)
        {
            rect.x = cont_rect.x + cont_rect.w - rect.w - padding_x - margin;
        }
        if (rect.y < cont_rect.y + padding + margin_top)
        {
            rect.y = tooltip.position.y + padding + 16; // For the pointer
        }
        if (rect.y > cont_rect.y + cont_rect.h - rect.h - padding - margin)
        {
            rect.y = cont_rect.y + cont_rect.h - rect.h - padding - margin;
        }

        const mu_Rect bg{
            rect.x - padding_x, rect.y - padding, rect.w + padding_x * 2, rect.h + padding * 2
        };

        mu_draw_rect(ctx, bg, mu_Color{0, 0, 0, 220});

        int line_y = 0;

        line_start = text;
        while (line_start - text < text_length)
        {
            const char* new_line =
                (const char*)memchr(line_start, '\n', text_length - (line_start - text));
            const char* line_end = new_line != nullptr ? new_line : text + text_length;
            const int line_length = line_end - line_start;

            mu_draw_text(
                ctx, ctx->style->font, line_start, line_length, mu_Vec2{rect.x, rect.y + line_y},
                {255, 255, 255, 255});

            line_y += interline_height + line_height;

            line_start = line_end + 1;
        }
    }
}

void draw_progress_bar(
    mu_Context* ctx,
    float progress,
    const mu_Color& left_color,
    const mu_Color& right_color,
    TooltipContext* tooltip_ctx)
{
    progress = hrz::clamp(progress, 0.0F, 1.0F);

    const mu_Rect full_rect = mu_layout_next(ctx);
    mu_draw_rect(ctx, full_rect, right_color);

    mu_Rect rect = full_rect;
    rect.w = (int)((float)rect.w * progress);
    mu_draw_rect(ctx, rect, left_color);

    mu_draw_box(ctx, full_rect, mu_Color{0, 0, 0, 255});

    if (tooltip_ctx && mu_mouse_over(ctx, full_rect))
    {
        char buffer[8];
        auto res = fmt::format_to(buffer, "{:.2f}%", progress * 100);
        add_tooltip(ctx, tooltip_ctx, {buffer, (size_t)std::distance(buffer, res.out)});
    }
}

int begin_layout_treenode(
    mu_Context* ctx,
    void* id_data,
    size_t id_size,
    hrz::flat_hash_set<mu_Id>& expanded_nodes)
{
    const mu_Id id = mu_get_id(ctx, id_data, (int)id_size);

    static int outer_layout[] = {-1};
    mu_layout_row(ctx, 1, outer_layout, 0);

    const mu_Rect rect = mu_layout_next(ctx);
    mu_layout_set_next(ctx, rect, 0);

    mu_layout_begin_column(ctx);

    static int inner_layout[] = {rect.h, -1};
    mu_layout_row(ctx, 2, inner_layout, 0);
    mu_layout_next(ctx);

    mu_update_control(ctx, id, rect, 0);

    bool expanded = expanded_nodes.count(id) > 0;

    if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->focus == id)
    {
        expanded = !expanded;
        if (expanded)
        {
            expanded_nodes.insert(id);
        }
        else
        {
            expanded_nodes.erase(id);
        }
    }

    if (ctx->hover == id)
    {
        ctx->draw_frame(ctx, rect, MU_COLOR_BUTTONHOVER);
    }

    mu_draw_icon(
        ctx, expanded ? MU_ICON_EXPANDED : MU_ICON_COLLAPSED,
        mu_rect(rect.x, rect.y, rect.h, rect.h), ctx->style->colors[MU_COLOR_TEXT]);

    mu_layout_begin_column(ctx);

    return expanded;
}

void end_layout_treenode_header(mu_Context* ctx, bool expanded)
{
    mu_layout_end_column(ctx);
    mu_layout_end_column(ctx);

    if (expanded)
    {
        static int layout[] = {ctx->style->indent, -1};
        mu_layout_row(ctx, 2, layout, 0);
        mu_layout_next(ctx);
        mu_layout_begin_column(ctx);
    }
}

void end_layout_treenode(mu_Context* ctx, bool expanded)
{
    if (expanded)
    {
        mu_layout_end_column(ctx);
    }
}

} // namespace hrz::ui
