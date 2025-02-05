#pragma once

layout(std140) uniform Gizmo
{
    mat4 transform;
    vec4 color;
    float alpha_fadeout;
} hrz_gizmo;
