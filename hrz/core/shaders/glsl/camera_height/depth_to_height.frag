#include "common/ubo_frame.glsl"
#include "planet/dtm_height.glsl"

uniform highp usampler2DArray u_dtm_indirection;
uniform highp sampler2D u_dtm_atlas;

uniform highp sampler2D u_camera_depth;

layout(std140) uniform CameraPosition
{
    vec2 wmerc_high;
    vec2 wmerc_low;
} hrz_camera;

layout(location = 0) out float o_height;

// Turn the depth that has been written to the camera height
// depth texture into a distance.
// This function reverses the orthographic matrix and the
// remapping of depth values from [-1, 1] to [0, 1].
float depth_to_distance(float depth_0_1)
{
    float z = 2.0 * depth_0_1 - 1.0;
    float near = float(HRZ_S_NEAR);
    float far = hrz_frame.view_elevation + float(HRZ_S_CAMERA_HEIGHT_FAR_OFFSET);
    return (z * (far - near) + far + near) / 2.0;
}

void main()
{
    float depth_height = depth_to_distance(texelFetch(u_camera_depth, ivec2(0, 0), 0).r);

    float ground_elevation = compute_height_from_dtm(
        u_dtm_indirection, u_dtm_atlas, hrz_camera.wmerc_high, hrz_camera.wmerc_low);
    float over_ground_height = hrz_frame.view_elevation - ground_elevation;

    o_height = min(depth_height, over_ground_height);
}
