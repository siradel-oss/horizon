#pragma once

float round_to_power_of_two(float v)
{
    return pow(2.0, round(log2(v)));
}
