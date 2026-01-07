layout(location = 0) in vec4 i_in_text_position_uv_0; // Bottom-left corner of the glyph
layout(location = 1) in vec4 i_in_text_position_uv_1; // Bottom-right
layout(location = 2) in vec4 i_in_text_position_uv_2; // Top-right
layout(location = 3) in vec4 i_in_text_position_uv_3; // Top-left
layout(location = 4) in uint i_text_index;

#define varying out
#include "symbol/text/interface.glsl"

#include "symbol/text/defs.glsl"
#include "symbol/common.vert.glsl"

uniform highp usampler2D u_anchor_indices;
uniform sampler2D u_transforms;

#if defined(SYMBOL_TEXT_OUTLINE) || defined(SYMBOL_PICKING) || defined(SYMBOL_SELECTION)
uniform sampler2D u_outline_widths;
#endif

#if defined(SYMBOL_TEXT_OUTLINE) || defined(SYMBOL_PICKING) || defined(SYMBOL_SELECTION)
uniform sampler2D u_outline_colors;
#endif

uniform sampler2D u_fill_colors;

void main()
{
    ivec2 data_coords = ivec2(int(i_text_index) % DATA_TEXTURE_SIZE, int(i_text_index) / DATA_TEXTURE_SIZE);

#if defined(SYMBOL_TEXT_OUTLINE) || defined(SYMBOL_PICKING) || defined(SYMBOL_SELECTION)
    float outline_width = texelFetch(u_outline_widths, data_coords, 0).r;

    if (outline_width <= 0.0)
    {
#if defined(SYMBOL_TEXT_OUTLINE)
        discard_vertex();
#elif defined(SYMBOL_PICKING) || defined(SYMBOL_SELECTION)
        outline_width = 0.0;
#endif
    }
#endif

    uint anchor_index = texelFetch(u_anchor_indices, data_coords, 0).r;
    handle_visibility(anchor_index);

    Anchor anchor = fetch_anchor(anchor_index);

    handle_selection(anchor);

    ivec2 tranform_data_coords = ivec2((int(i_text_index) % DATA_TEXTURE_SIZE) * 4, int(i_text_index) / DATA_TEXTURE_SIZE);

    mat4 transform = mat4(
        texelFetch(u_transforms, tranform_data_coords + ivec2(0, 0), 0),
        texelFetch(u_transforms, tranform_data_coords + ivec2(1, 0), 0),
        texelFetch(u_transforms, tranform_data_coords + ivec2(2, 0), 0),
        texelFetch(u_transforms, tranform_data_coords + ivec2(3, 0), 0)
    );

    vec2 in_text_position;
    vec2 uv;
    if (gl_VertexID == 0)
    {
        in_text_position = i_in_text_position_uv_0.xy;
        uv = i_in_text_position_uv_0.zw;
    }
    else if (gl_VertexID == 1)
    {
        in_text_position = i_in_text_position_uv_1.xy;
        uv = i_in_text_position_uv_1.zw;
    }
    else if (gl_VertexID == 2)
    {
        in_text_position = i_in_text_position_uv_2.xy;
        uv = i_in_text_position_uv_2.zw;
    }
    else
    {
        in_text_position = i_in_text_position_uv_3.xy;
        uv = i_in_text_position_uv_3.zw;
    }

    vec4 in_element_pos = transform * vec4(in_text_position, 0, 1);
    anchor_vertex(anchor, in_element_pos);

    v_uv = write_perspective_uv(uv, in_element_pos.w);

#ifdef SYMBOL_VISUAL
#   ifdef SYMBOL_TEXT_FILL
    v_color = texelFetch(u_fill_colors, data_coords, 0);
#   endif

#   ifdef SYMBOL_TEXT_OUTLINE
    v_color = texelFetch(u_outline_colors, data_coords, 0);
#   endif

    v_color.rgb *= v_color.a;
#elif defined(SYMBOL_PICKING) || defined(SYMBOL_SELECTION)
    float outline_alpha = texelFetch(u_outline_colors, data_coords, 0).a;
    if (outline_alpha == 0.0 || outline_width == 0.0)
    {
        outline_width = 0.0;

        float fill_alpha = texelFetch(u_fill_colors, data_coords, 0).a;
        if (fill_alpha == 0.0)
        {
            discard_vertex();
        }
    }

    v_color = vec4(1.0, 1.0, 1.0, 1.0);
#endif

#if defined(SYMBOL_TEXT_OUTLINE) || defined(SYMBOL_PICKING) || defined(SYMBOL_SELECTION)
    v_outline_width = outline_width;
#endif
}
