#include "common/ubo_frame.glsl"
#include "common/camera.glsl"
#include "common/viewshed.vert.glsl"
#include "common/clip.vert.glsl"
#include "common/colors.glsl"
#include "common/sun_shadows.vert.glsl"
#include "common/octahedral.glsl"

layout(location = 0) in vec2 i_position;
layout(location = 1) in vec4 i_color;
layout(location = 2) in vec3 i_scale;
layout(location = 3) in vec3 i_impostor_position;
layout(location = 4) in uvec2 i_impostor_orientation;
layout(location = 5) in uvec2 i_feature_id;
layout(location = 6) in uint i_object_id;
layout(location = 7) in uint i_selection;

#include "impostors/defs.glsl"

#define varying out
#include "impostors/interface.glsl"

uniform highp sampler2D hrz_impostor_scale_coefficients_texture;

vec2 octa_to_uv(vec3 octa)
{
    vec3 d = normalize(octa);
    // Go back to "un-puffed" octahedron (|x| + |y| + |z| = 1)
    vec3 octant = sign(d);
    float sum = dot(d, octant);
    vec3 pyramid = d / sum;

    // Flatten to 2D UVs.
    return vec2(pyramid.x - pyramid.y, pyramid.x + pyramid.y) * 0.5 + 0.5;
}

vec3 uv_to_octa(vec2 uv)
{
    uv = uv * 2.0 - 1.0;
    vec3 pos = vec3(uv.x + uv.y, -uv.x + uv.y, 0) * 0.5;
    pos.z = 1.0 - abs(pos.x) - abs(pos.y);
    return normalize(pos);
}

void make_frame_xy_basis(vec3 normal, out vec3 x, out vec3 y)
{
    vec3 up = vec3(0, 0, 1);
    if (normal.z > 0.99999)
    {
        up = vec3(0, 1, 0);
    }
    x = normalize(cross(up, normal));
    y = normalize(cross(normal, x));
}

void main()
{
#ifdef IMPOSTOR_SELECTION
    uint bit_index = uint(gl_InstanceID) % 32u;
    if ((i_selection & (1u << bit_index)) == 0u)
    {
        gl_Position = vec4(0);
        return;
    }
#endif

#ifdef IMPOSTOR_VISUAL
    v_feature_id = i_feature_id;
#endif

#ifdef IMPOSTOR_PICKING
    v_object_id = i_object_id;
#endif

    v_color = srgb_to_linear(i_color);

    vec3 right = octahedral_decompress_normal(i_impostor_orientation.x);
    vec3 up = octahedral_decompress_normal(i_impostor_orientation.y);
    vec3 forward = normalize(cross(right, up));

    mat3 world_to_impostor = mat3(right, up, forward);
    mat3 impostor_to_world = transpose(world_to_impostor);
    mat3 impostor_to_view = mat3(hrz_frame.view_matrix) * impostor_to_world;

    vec3 in_tile_pos = i_impostor_position;
    vec3 world_pos = translate_relative_to_camera(in_tile_pos, hrz_tile.center_low.xyz, hrz_tile.center_high.xyz);

    // Apply offset to reflect the fact that the model isn't necessarily centered at (0, 0, 0).
    world_pos = world_to_impostor * world_pos;
    world_pos = impostor_to_world * (world_pos + hrz_impostor.offset_from_origin * i_scale);

    vec4 center_view = hrz_frame.view_cc_matrix * vec4(world_pos, 1);
    do_clipping(center_view, hrz_tile.clip_id);

#ifdef IMPOSTOR_VISUAL
    if (hrz_tile.lighting_enabled)
    {
        vec3 global_pos = translate(in_tile_pos, hrz_tile.center_low.xyz, hrz_tile.center_high.xyz);
        v_altitude = length(global_pos);
        v_normal_to_ground = mat3(hrz_frame.view_matrix) * (global_pos / v_altitude);
        v_impostor_to_view_x = impostor_to_view[0];
        v_impostor_to_view_y = impostor_to_view[1];
        v_impostor_to_view_z = impostor_to_view[2];
    }
    else
    {
        v_normal_to_ground = vec3(1.0);
        v_altitude = 0.0;
        v_impostor_to_view_x = vec3(1.0, 0.0, 0.0);
        v_impostor_to_view_y = vec3(0.0, 1.0, 0.0);
        v_impostor_to_view_z = vec3(0.0, 0.0, 1.0);
    }
#endif

    // Get the vector from the impostor to the camera.`
    vec3 to_camera = normalize(-world_pos);

    // Transform the viewing vector into the original coordinate system the model
    // was baked in.
    vec3 d = world_to_impostor * to_camera;

    vec2 max_frame = vec2(hrz_impostor.atlas_size) - 1.0;
    vec2 inv_max_frame = 1.0 / max_frame;

    // From there, we transform the view direction into the coordinate in the hemi-octahedron atlas.
    // And we round this to get the coordinates of the baked frame closest to the viewing direction.
    vec2 frame = floor(octa_to_uv(d) * max_frame + vec2(0.5));

    // Get the scaling coefficients corresponding to the impostor frame
    int frame_index = int(frame.x) + int(frame.y) * int(hrz_impostor.atlas_size.x);

    // See the documentation of scale_coefficients in the impostors baker for more info.
    vec3 scale_coeffs_x = texelFetch(hrz_impostor_scale_coefficients_texture, ivec2(frame_index * 2, 0), 0).xyz;
    vec3 scale_coeffs_y = texelFetch(hrz_impostor_scale_coefficients_texture, ivec2(frame_index * 2 + 1, 0), 0).xyz;

    // The scale applied to dir0_x and dir0_y, the axes of the quad used to render the impostor
    // texture in the world
    float final_scale_x = length(i_scale * scale_coeffs_x);
    float final_scale_y = length(i_scale * scale_coeffs_y);

    // We get the vector the frame was baked in, in the impostor space.
    // And then compute the x and y basis vectors of the plane on which the
    // impostor was baked for the chosen frame.
    vec3 dir0_imp = uv_to_octa(frame * inv_max_frame);
    vec3 dir0_x_imp, dir0_y_imp;
    make_frame_xy_basis(dir0_imp, dir0_x_imp, dir0_y_imp);

    // We transform back the basis of the impostor frame plane into world space.
    vec3 dir0_x = impostor_to_world * dir0_x_imp;
    vec3 dir0_y = impostor_to_world * dir0_y_imp;

    // Finally we form a plane with the basis vectors we computed to apply the
    // baked frame onto.
    vec3 p = world_pos + (dir0_x * i_position.x * final_scale_x + dir0_y * i_position.y * final_scale_y) * hrz_impostor.scale_correction;
    do_viewshed(hrz_frame.view_cc_matrix * vec4(p, 1));
    gl_Position = hrz_frame.pv_cc_matrix * vec4(p, 1);

    const vec2 uvs[4] = vec2[](vec2(1, 1), vec2(1, 0), vec2(0, 1), vec2(0, 0));
    v_uv = uvs[gl_VertexID];
    v_frame = frame;
}
