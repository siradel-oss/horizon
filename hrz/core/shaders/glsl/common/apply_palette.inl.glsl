#if defined(WORKAROUND_004)
// @Workaround(004-Safari-UniformBufferArrayLoad)
#define CPV(I) (palette.packed_color_points_value[(I) / 4][(I) % 4])
#define CPE(I) (palette.color_points_encoded[(I)])
#else // defined(WORKAROUND_004)
// @Workaround(005-Android-LoadDataToStructure)
#define CPV(I) (PALETTE_UBO_PATH.packed_color_points_value[(I) / 4][(I) % 4])
#define CPE(I) (PALETTE_UBO_PATH.color_points_encoded[(I)])
#endif // !defined(WORKAROUND_004)

vec4 PALETTE_FN_NAME(PALETTE_ADDITIONAL_ARGUMENTS float value)
{
#if defined(WORKAROUND_004)
    Palette palette = PALETTE_UBO_PATH;
#endif // defined(WORKAROUND_004)

    vec4 nan_color = PALETTE_UBO_PATH.nan_color;
    int color_interpolation_mode = PALETTE_UBO_PATH.color_interpolation_mode;
    int num_color_points = PALETTE_UBO_PATH.num_color_points;

    if (num_color_points == 0 || is_nan(value))
    {
        return nan_color;
    }

    if (value < CPV(0))
    {
        return decode(color_interpolation_mode, CPE(0));
    }
    else if (value >= CPV(num_color_points - 1))
    {
        return decode(color_interpolation_mode, CPE(2 * num_color_points - 1));
    }

    int low = 0;
    int high = int(num_color_points - 1);
    int max_iter = int(num_color_points / 2);

    for (int i = 0; i < HRZ_S_MAX_PALETTE_COLOR_POINTS / 2; ++i)
    {
        if (i >= max_iter || low >= high - 1) break;

        int mid = (high + low) / 2;
        if (value >= CPV(mid))
        {
            low = mid;
        }
        else
        {
            high = mid;
        }
    }

    float delta = (value - CPV(low)) / (CPV(high) - CPV(low));

    vec4 lower_color = CPE(2 * low + 1);
    vec4 upper_color = CPE(2 * high);

    if (color_interpolation_mode == MODE_THRESHOLD)
    {
        delta = step(0.5, delta);
    }

    return decode(color_interpolation_mode, mix(lower_color, upper_color, delta));
}
