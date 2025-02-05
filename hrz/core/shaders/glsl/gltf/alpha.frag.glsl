#pragma once

void handle_alpha_discard(float alpha)
{
    if (alpha == 0.0) discard;
}

float handle_material_alpha_mode(uint mode, float cutoff, float alpha)
{
    switch (mode)
    {
        case ALPHA_MODE_OPAQUE: return 1.0;
        case ALPHA_MODE_MASK: return step(cutoff, alpha);
        default: return alpha;
    }
}
