#include "sky/sky_params_ubo.glsl"
#include "sky/sky.glsl"

layout(location = 0) out vec4 o_color;

uniform sampler2D u_sky_view;

// Marques, Ricardo, et al. "Spherical Fibonacci point sets for illumination
// integrals." Computer Graphics Forum. Vol. 32. No. 8. 2013.
vec3 fibonacci_sample(ivec2 uv)
{
    float i = float(uv.y * 64 + uv.x);
    float g = 1.6180339887498948482;
    float phi = acos(1.0 - (2.0 * i + 1.0) / (64.0 * 64.0));
    float theta = 2.0 * i * PI * g;
    float sin_phi = sin(phi);
    return vec3(
        cos(theta) * sin_phi,
        sin(theta) * sin_phi,
        cos(phi));
}

// Those coefficients come from
// Ramamoorthi, Ravi, and Pat Hanrahan. "An efficient representation for
// irradiance environment maps." Proceedings of the 28th annual conference on
// Computer graphics and interactive techniques. 2001.
float sh_coeff(int quadrant, vec3 d)
{
    if (quadrant == 0) // L_0,0
    {
        return 0.282095;
    }
    else if (quadrant == 1) // L_1,-1
    {
        return 0.488603 * d.y;
    }
    else if (quadrant == 2) // L_1,0
    {
        return 0.488603 * d.z;
    }
    else if (quadrant == 3) // L_1,1
    {
        return 0.488603 * d.x;
    }
    else if (quadrant == 4) // L_2,-2
    {
        return 1.092548 * d.x * d.y;
    }
    else if (quadrant == 5) // L_2,-1
    {
        return 1.092548 * d.y * d.z;
    }
    else if (quadrant == 6) // L_2,0
    {
        return 0.315392 * (3.0 * d.z * d.z - 1.0);
    }
    else if (quadrant == 7) // L_2,1
    {
        return 1.092548 * d.x * d.z;
    }
    else if (quadrant == 8) // L_2,2
    {
        return 0.54627 * (d.x * d.x - d.y * d.y);
    }
    return 1.0;
}

// To compute the spherical harmonics coefficients we need to integrate the
// whole environment. But we don't really want to do that. Instead we choose
// well distributed points on the unit sphere using the fibonacci sequence.
//
// We do this 9 times for the 9 coefficients we need to compute. So in the
// resulting image we have a 3x3 grid of 64x64 samples multiplied by their
// spherical harmonics weights.
//
// By summing those squares in subsequent passes we compute the spherical
// hamonics coefficients.
//
// King, Gary. "Real-time computation of dynamic irradiance environment maps."
// GPU Gems 2 (2005): 167-176.

void main()
{
    ivec2 quadrant = ivec2(floor(gl_FragCoord.xy / 64.0));
    ivec2 uv_i = ivec2(gl_FragCoord.xy) % ivec2(64);
    vec3 direction = fibonacci_sample(uv_i);
    vec2 uv = encode_sky_view_uv(direction, hrz_sky.horizon_horizon_angle);
    o_color = texture(u_sky_view, vec2(uv)) * sh_coeff(quadrant.y * 3 + quadrant.x, direction) * HRZ_S_SKY_EXPOSURE;
}
