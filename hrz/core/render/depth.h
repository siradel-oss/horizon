#pragma once

#include "hrz/common/shader_defines.h"

#include <cmath>

namespace hrz::render
{

template<typename T>
inline T log_depth(T depth)
{
    return std::log(depth / (T)HRZ_S_NEAR) / std::log((T)HRZ_S_FAR / (T)HRZ_S_NEAR);
}

template<typename T>
inline T undo_log_depth(T log_depth)
{
    return std::exp(log_depth * std::log((T)HRZ_S_FAR / (T)HRZ_S_NEAR)) * (T)HRZ_S_NEAR;
}

template<typename T>
inline T lin_depth(T depth)
{
    return (depth - HRZ_S_NEAR) / (HRZ_S_FAR - HRZ_S_NEAR);
}

} // namespace hrz::render
