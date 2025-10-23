#include "common/colors.glsl"
#include "common/ubo_frame.glsl"
#include "common/flat_overlay_cameras.glsl"
#include "common/camera_height.glsl"
#include "flat_vectors/defs.glsl"

layout(location = 0) in vec2 i_in_mesh_pos;
layout(location = 1) in vec3 i_position;
layout(location = 2) in vec4 i_color;
layout(location = 3) in float i_radius;
layout(location = 4) in uint i_feature_index;

#include "flat_vectors/common.vert.glsl"

void main()
{
#ifdef FLAT_SELECTION
    if (!fetch_selection())
    {
        gl_Position = vec4(0.0);
        return;
    }
#endif

    v_color = srgb_to_linear(i_color);
    v_uv = i_in_mesh_pos + vec2(0.5);

    float full_radius = i_radius + hrz_tile.disc_outline_width;
    float pixel_to_meter = fetch_camera_height() * hrz_frame.camera_height_to_perceived_distance * hrz_frame.pixel_size_in_meters;
    float scaled_radius = bool(hrz_tile.disc_radius_unit) ? pixel_to_meter * full_radius : full_radius;
    v_radius_px = scaled_radius * float(hrz_overlay_passes.texture_size) / hrz_overlay_passes.world_size;

#ifdef FLAT_VISUAL
    v_feature_id = fetch_feature_id();
#endif

#ifdef FLAT_PICKING
    v_feature_index = i_feature_index;
#endif

    v_disc_dist = (i_radius / full_radius) * 0.5;

    vec4 offset = vec4(translate_relative_to_overlay_cameras(hrz_tile.center_low.xyz, hrz_tile.center_high.xyz), 0.0);
    vec4 vertex = hrz_overlay_cameras.overlay_cams_view_cc * (vec4(i_position, 1.0) + offset);
    vertex.xy += (i_in_mesh_pos * 2.0 * scaled_radius);
    gl_Position = hrz_overlay_cameras.overlay_cams_proj[hrz_overlay_passes.pass_id] * vertex;
}
