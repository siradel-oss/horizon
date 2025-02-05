#pragma once

// From glm
mat4 euler_angles_xyz_to_mat4(vec3 angles)
{
    float c1 = cos(-angles.x);
    float c2 = cos(-angles.y);
    float c3 = cos(-angles.z);
    float s1 = sin(-angles.x);
    float s2 = sin(-angles.y);
    float s3 = sin(-angles.z);

    mat4 m;
    m[0][0] = c2 * c3;
    m[0][1] = -c1 * s3 + s1 * s2 * c3;
    m[0][2] = s1 * s3 + c1 * s2 * c3;
    m[0][3] = 0.0;
    m[1][0] = c2 * s3;
    m[1][1] = c1 * c3 + s1 * s2 * s3;
    m[1][2] = -s1 * c3 + c1 * s2 * s3;
    m[1][3] = 0.0;
    m[2][0] = -s2;
    m[2][1] = s1 * c2;
    m[2][2] = c1 * c2;
    m[2][3] = 0.0;
    m[3][0] = 0.0;
    m[3][1] = 0.0;
    m[3][2] = 0.0;
    m[3][3] = 1.0;
    return m;
}
