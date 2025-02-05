#pragma once

uniform sampler2D u_camera_height;

float fetch_camera_height()
{
    return texelFetch(u_camera_height, ivec2(0, 0), 0).r;
}
