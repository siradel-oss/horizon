#pragma once

const float PI = 3.14159265359;

// isnan doesn't always work in GLSL/WebGL so here we are...
bool is_nan(float val)
{
    if (isnan(val)) return true;
    return (val < 0.0 || 0.0 < val || val == 0.0) ? false : true;
}
