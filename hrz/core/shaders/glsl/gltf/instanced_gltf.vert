#define varying out

#include "common/maths.glsl"
#include "common/ubo_frame.glsl"
#include "common/normal_matrix.glsl"

#ifdef GLTF_DEPTH
#include "common/ubo_view.glsl"
#endif

#include "gltf/defs.glsl"
#include "gltf/attributes.glsl"

#define varying out
#include "gltf/interface.glsl"

#include "gltf/common.vert.glsl"

uniform highp sampler2D u_instance_position;
uniform highp usampler2D u_instance_compressed_position;
uniform highp sampler2D u_instance_normal;
uniform highp usampler2D u_instance_compressed_normal;
uniform highp sampler2D u_instance_scale;
uniform lowp sampler2D u_instance_color;
uniform highp usampler2D u_selection;

#ifdef GLTF_VISUAL
uniform highp usampler2D u_instance_feature_id;
#endif

#ifdef GLTF_PICKING
uniform highp usampler2D u_instance_object_id;
#endif

bool fetch_selection()
{
    int bucket_index = gl_InstanceID / 32;
    ivec2 coord = ivec2(bucket_index % 2048, bucket_index / 2048);
    int bit_index = gl_InstanceID % 32;
    uint bitmask = texelFetch(u_selection, coord, 0).r;
    return (bitmask & (1u << bit_index)) != 0u;
}

ivec2 get_pixel_coords(int instance)
{
    return ivec2(instance % DATA_TEXTURE_SIZE, instance / DATA_TEXTURE_SIZE);
}

vec3 fetch_instance_position()
{
    ivec2 instance_data_coords = get_pixel_coords(gl_InstanceID);

    if (hrz_instance_group.position_compression.type == COMPRESSION_QUANTIZED)
    {
        vec3 p = vec3(texelFetch(u_instance_compressed_position, instance_data_coords, 0).xyz);
        return p * hrz_instance_group.position_compression.quantization_scale
            + hrz_instance_group.position_compression.quantization_mins;
    }
    else
    {
        return texelFetch(u_instance_position, instance_data_coords, 0).xyz;
    }
}

mat3 fetch_instance_normals()
{
    if (hrz_instance_group.normal_compression.type == COMPRESSION_OCT_ENCODED)
    {
        vec4 normals = vec4(texelFetch(u_instance_compressed_normal, get_pixel_coords(gl_InstanceID), 0));
        vec3 right = quantized_octahedral_coords_to_unit_vector(
            normals.xy, hrz_instance_group.normal_compression.quantization_scale.xy);
        vec3 up = quantized_octahedral_coords_to_unit_vector(
            normals.zw, hrz_instance_group.normal_compression.quantization_scale.xy);
        vec3 forward = normalize(cross(right, up));
        return mat3(right, up, forward);
    }
    else
    {
        vec3 right = texelFetch(u_instance_normal, get_pixel_coords(2 * gl_InstanceID + 0), 0).xyz;
        vec3 up = texelFetch(u_instance_normal, get_pixel_coords(2 * gl_InstanceID + 1), 0).xyz;
        vec3 forward = normalize(cross(right, up));
        return mat3(right, up, forward);
    }
}

// Instance textures (like the color and scale) can be of size 1x1 when all the instances share the
// same value. In this case, instance pixel coordinates are outside of the texture size and need to
// be wrapped. `texelFetch` ignores sampler parameters like the wrapping strategy for out-of-bounds
// reads so we need to do it manually.
vec4 texel_fetch_wrap(sampler2D sampler, ivec2 coords)
{
    return texelFetch(sampler, coords % textureSize(sampler, 0), 0);
}

mat4 instance_transform()
{
    vec3 position = fetch_instance_position();
    mat3 normals = fetch_instance_normals();
    vec3 scales = texel_fetch_wrap(u_instance_scale, get_pixel_coords(gl_InstanceID)).xyz;

    return mat4(
        vec4(normals[0] * scales.x, 0),
        vec4(normals[1] * scales.y, 0),
        vec4(normals[2] * scales.z, 0),
        vec4(position, 1));
}

mat3 instance_normal_transform(mat4 instance_transform)
{
    return compute_normal_matrix(mat3(instance_transform));
}

void main()
{
#ifdef GLTF_SELECTION
    if (!fetch_selection())
    {
        // Create 0-sized triangles to prevent drawing the
        // non-selected instances.
        gl_Position = vec4(0);
        return;
    }
#endif

    mat4 instance_transform = instance_transform();

    vec4 position = hrz_prim_transform.transform * vec4(fetch_position(), 1);
    position.xyz += hrz_prim_transform.origin_low.xyz;
    position.xyz += hrz_prim_transform.origin_high.xyz;
    position = mat4(hrz_instance_group.linear_transform) * instance_transform * position;

    vec4 pos_cc = vec4(translate_relative_to_camera(position.xyz, hrz_instance_group.origin_low.xyz, hrz_instance_group.origin_high.xyz), 1);
    vec4 view_pos = hrz_frame.view_cc_matrix * pos_cc;

    output_position(pos_cc, view_pos);

#ifdef GLTF_VISUAL
    v_view_pos = view_pos.xyz;

    mat3 instance_normal_transform = instance_normal_transform(instance_transform);
    output_normal(instance_normal_transform);
    output_geometry_decoration(pos_cc.xyz, view_pos.xyz, v_normal);
#endif

    ivec2 instance_data_coords = get_pixel_coords(gl_InstanceID);

#ifdef GLTF_VISUAL
    v_feature_id = texelFetch(u_instance_feature_id, instance_data_coords, 0).rg;
#endif

#ifdef GLTF_PICKING
    v_object_id = texelFetch(u_instance_object_id, instance_data_coords, 0).r;
#endif

    output_uv_and_color();
    v_feature_color = texel_fetch_wrap(u_instance_color, instance_data_coords);
}
