#pragma once

layout(location = 0) in vec3 i_position;
layout(location = 1) in uvec3 i_compressed_position;
layout(location = 2) in vec3 i_normal;
layout(location = 3) in uvec3 i_compressed_normal;
layout(location = 4) in vec4 i_color;
layout(location = 6) in vec2 i_uv_0;
layout(location = 7) in uvec2 i_compressed_uv_0;
layout(location = 8) in vec2 i_uv_1;
layout(location = 9) in uvec2 i_compressed_uv_1;

float sign_not_zero(float value)
{
    return value >= 0.0 ? 1.0 : -1.0;
}

vec2 sign_not_zero(vec2 value)
{
    return vec2(sign_not_zero(value.x), sign_not_zero(value.y));
}

// See Cigolle et al., 2014
// http://jcgt.org/published/0003/02/01/
vec3 quantized_octahedral_coords_to_unit_vector(vec2 encoded, vec2 scale)
{
    if (encoded.x == 0.0 && encoded.y == 0.0) {
        return vec3(0.0, 0.0, 0.0);
    }

    // Scale values are shifted from [0,2] to [-1,1].
    encoded = encoded * scale - 1.0;
    vec3 v = vec3(encoded.x, encoded.y, 1.0 - abs(encoded.x) - abs(encoded.y));
    if (v.z < 0.0)
    {
        v.xy = (1.0 - abs(v.yx)) * sign_not_zero(v.xy);
    }

    return normalize(v);
}

// Oct-encoded data are used in both Draco and Cesium's 3D tiles (i3dm) but both don't use the same
// projection plane convention.
// - https://github.com/pmconne/cesium/blob/a4be986c04b863e97e251c3fc6b78f106f7de4d7/Source/Core/AttributeCompression.js#L41
// - https://github.com/google/draco/blob/7d58126d076bc3f5f9d8c114d1700b7311faecfe/src/draco/compression/attributes/normal_compression_utils.h#L299
vec3 draco_quantized_octahedral_coords_to_unit_vector(vec2 encoded, vec2 scale)
{
    return quantized_octahedral_coords_to_unit_vector(encoded, scale).zxy;
}

vec3 fetch_position()
{
    if (hrz_prim_draw.geometry.position_compression.type == COMPRESSION_QUANTIZED)
    {
        return vec3(i_compressed_position) * hrz_prim_draw.geometry.position_compression.quantization_scale
            + hrz_prim_draw.geometry.position_compression.quantization_mins;
    }
    else if (hrz_prim_draw.geometry.position_compression.type == COMPRESSION_OCT_ENCODED)
    {
        return draco_quantized_octahedral_coords_to_unit_vector(
            vec2(i_compressed_position.xy),
            hrz_prim_draw.geometry.position_compression.quantization_scale.xy);
    }
    else
    {
        return i_position;
    }
}

vec3 fetch_normal()
{
    if (hrz_prim_draw.geometry.normal_compression.type == COMPRESSION_QUANTIZED)
    {
        return vec3(i_compressed_normal) * hrz_prim_draw.geometry.normal_compression.quantization_scale
            + hrz_prim_draw.geometry.normal_compression.quantization_mins;
    }
    else if (hrz_prim_draw.geometry.normal_compression.type == COMPRESSION_OCT_ENCODED)
    {
        return draco_quantized_octahedral_coords_to_unit_vector(
            vec2(i_compressed_normal.xy),
            hrz_prim_draw.geometry.normal_compression.quantization_scale.xy);
    }
    else
    {
        return i_normal;
    }
}

vec4 fetch_color()
{
    return i_color;
}

vec2 fetch_uv(in vec2 uncompressed, in uvec2 compressed, in VertexCompressionParams compression)
{
    if (compression.type == COMPRESSION_QUANTIZED)
    {
        return vec2(compressed) * compression.quantization_scale.xy + compression.quantization_mins.xy;
    }
    else if (compression.type == COMPRESSION_OCT_ENCODED)
    {
        return draco_quantized_octahedral_coords_to_unit_vector(vec2(compressed.xy), compression.quantization_scale.xy).xy;
    }
    else
    {
        return uncompressed;
    }
}
