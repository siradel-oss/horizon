#include "common/ubo_frame.glsl"
#include "common/camera.glsl"
#include "common/camera_height.glsl"
#include "common/polylines.vert.glsl"
#include "common/clip.vert.glsl"
#include "common/octahedral.glsl"
#include "common/sdf.glsl"
#include "common/colors.glsl"
#include "cylinders/defs.glsl"

#ifdef CYLINDER_VISUAL
#   include "common/sun_shadows.vert.glsl"
#   include "common/viewshed.vert.glsl"
#endif

#ifdef CYLINDER_DEPTH
#   include "common/ubo_view.glsl"
#endif

layout(location = 0) in vec3 i_vertex_pos;
layout(location = 1) in vec4 i_color;
layout(location = 2) in vec2 i_radii;
layout(location = 3) in vec3 i_instance_pos0;
layout(location = 4) in uint i_normal0;
layout(location = 5) in vec3 i_instance_pos1;
layout(location = 6) in uint i_normal1;
layout(location = 7) in vec4 i_geometry;
layout(location = 8) in float i_animation_speed;
layout(location = 9) in vec4 i_secondary_color;
layout(location = 10) in float i_total_length;
layout(location = 11) in uint i_feature_index;
layout(location = 12) in uvec2 i_feature_id;
layout(location = 13) in uint i_selection;

#define varying out
#include "cylinders/interface.glsl"

float compute_dash_size_unit_coef(uint dash_size_unit, float pixel_to_meter)
{
    float coef = 1.0f;
    if (dash_size_unit == DASH_SIZE_UNIT_PIXELS)
    {
        // This is for the stepping function for polylines dash period sizes trying
        // to enhance stability when zooming in/out or tilting.
        float step = pow(2.0, floor(log2(pixel_to_meter)));
        coef = step;
    }
    else if (dash_size_unit == DASH_SIZE_UNIT_RELATIVE)
    {
        coef = i_total_length;
    }
    return coef;
}

void main()
{
#ifdef CYLINDER_SELECTION
    int bit_index = gl_InstanceID % 32;
    bool is_visible = (i_selection & (1u << bit_index)) != 0u;

    if (!is_visible)
    {
        gl_Position = vec4(0);
        return;
    }
#endif

#ifdef CYLINDER_DEPTH
    mat4 pv_cc = hrz_view.pv_cc;
#else
    mat4 pv_cc = hrz_frame.pv_cc_matrix;
#endif

    vec3 floor_normal = normalize(hrz_tile.center_high.xyz + hrz_tile.center_low.xyz);

    vec3 normal0 = octahedral_decompress_normal(i_normal0);
    vec3 normal1 = octahedral_decompress_normal(i_normal1);

    // We first compute an orthonormal basis for the cylinder in world space
    vec3 z_cyl = normal0;
    if (i_instance_pos1 != i_instance_pos0)
    {
        z_cyl = normalize(i_instance_pos1 - i_instance_pos0);
    }

    // We try to make the rotation of the cylinders as stable as possible,
    // so as to minimizes the small gaps between segments.
    vec3 x_cyl = floor_normal;
    if (abs(dot(x_cyl, z_cyl)) > 0.99999)
    {
        x_cyl = vec3(z_cyl.x, z_cyl.z, -z_cyl.y);
    }
    vec3 y_cyl = normalize(cross(z_cyl, x_cyl));
    x_cyl = normalize(cross(y_cyl, z_cyl));

    // From this basis, we can compute a rotation matrix that transforms the
    // unit cylinder into world space.
    mat3 rotation = mat3(x_cyl, y_cyl, z_cyl);

    float radius = mix(i_radii.x, i_radii.y, i_vertex_pos.z);

    // We apply the rotation to the cylinder, and also scale it in the xy plane
    // according to its radius, and on the z axis according to the length of the segment.
    vec3 position = rotation * (i_vertex_pos * vec3(radius, radius, length(i_instance_pos1 - i_instance_pos0))) + i_instance_pos0;

    // First select the correct joint position and normal, depending on what end
    // of the cylinder we're processing.
    vec3 joint_point = mix(i_instance_pos0, i_instance_pos1, i_vertex_pos.z);
    vec3 joint_normal = mix(normal0, normal1, i_vertex_pos.z);
    vec3 segment_direction = z_cyl;

    float joint_offset = project_point_to_plane_along_direction_distance(position, joint_point, joint_normal, segment_direction);
    position = position + segment_direction * joint_offset;

    vec4 pos_cc = vec4(translate_relative_to_camera(position, hrz_tile.center_low.xyz, hrz_tile.center_high.xyz), 1);
    vec4 view_pos = hrz_frame.view_cc_matrix * pos_cc;
    do_clipping(view_pos, hrz_tile.clip_id);

    gl_Position = pv_cc * pos_cc;

    vec3 pos0_cc = translate_relative_to_camera(i_instance_pos0, hrz_tile.center_low.xyz, hrz_tile.center_high.xyz);
    vec3 pos1_cc = translate_relative_to_camera(i_instance_pos1, hrz_tile.center_low.xyz, hrz_tile.center_high.xyz);
    float dist = sdf_segment3(vec3(0), pos0_cc, pos1_cc);
    float cam_height = fetch_camera_height() * hrz_frame.camera_height_to_perceived_distance;
    float speed_pixel_to_meter = cam_height * hrz_frame.pixel_size_in_meters;
    float size_pixel_to_meter = dist * hrz_frame.pixel_size_in_meters;

    float dash_period_unit_coef = compute_dash_size_unit_coef(hrz_tile.dash_period_unit, size_pixel_to_meter);
    float dash_primary_length_unit_coef = compute_dash_size_unit_coef(hrz_tile.dash_primary_length_unit, size_pixel_to_meter);
    float animation_unit_coef = compute_dash_size_unit_coef(hrz_tile.animation_speed_unit, speed_pixel_to_meter);

    float dash_meter_period = i_geometry.z * dash_period_unit_coef;
    float animation_advance = i_animation_speed * animation_unit_coef * hrz_frame.time;

    v_pos_along_line = mix(i_geometry.x, i_geometry.y, i_vertex_pos.z);
    v_pos_along_line += joint_offset;
    v_pos_along_line = (v_pos_along_line - animation_advance) / dash_meter_period;
    v_dash_ratio = i_geometry.w / i_geometry.z * dash_primary_length_unit_coef / dash_period_unit_coef;
    v_invert_gradient_direction = i_animation_speed < 0.0 ? 1u : 0u;

    v_color_oklab = i_color;
    v_secondary_color_oklab = i_secondary_color;

#ifdef CYLINDER_VISUAL
    mat3 normal_matrix = mat3(hrz_frame.view_matrix);

    // Since it's a unit cylinder, the xy positions of the vertices are also the normals.
    // Also the rotation matrix is a pure rotation matrix, so it's the normal matrix.
    // Easy!
    v_normal = normal_matrix * rotation * vec3(i_vertex_pos.xy, 0);

    do_sun_shadows(view_pos);
    do_viewshed(view_pos);
    compute_clip_outline_attenuation(hrz_tile.clip_id, v_normal);

    vec3 pos_global = translate(position, hrz_tile.center_low.xyz, hrz_tile.center_high.xyz);
    v_altitude = length(pos_global);
    v_normal_to_ground = normal_matrix * (pos_global / v_altitude);
#endif

#ifdef CYLINDER_VISUAL
    v_feature_id = i_feature_id;
#endif

#ifdef CYLINDER_PICKING
    v_feature_index = i_feature_index;
#endif
}
