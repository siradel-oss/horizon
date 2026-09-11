// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/global_flags.h"

static bool g_flag_values[(int)hrz::Flag::_FlagCount] = {};

namespace hrz
{

void set_flag(Flag flag, bool value)
{
    g_flag_values[(int)flag] = value;
}

bool get_flag(Flag flag)
{
    return g_flag_values[(int)flag];
}

void iterate_flags(const std::function<void(const char*, bool)>& callback)
{
#define HRZ_DEFINE_FLAG(NAME) callback(#NAME, get_flag(Flag::NAME));
    HRZ_FLAGS
#undef HRZ_DEFINE_FLAG
}

#define HRZ_DEFINE_VALUE(NAME, DEFAULT, MIN, MAX) DEFAULT,
static float g_value_values[(int)hrz::Value::_ValueCount] = {HRZ_VALUES};
#undef HRZ_DEFINE_VALUE

#define HRZ_DEFINE_VALUE(NAME, DEFAULT, MIN, MAX) MIN,
static const float g_value_mins[(int)hrz::Value::_ValueCount] = {HRZ_VALUES};
#undef HRZ_DEFINE_VALUE

#define HRZ_DEFINE_VALUE(NAME, DEFAULT, MIN, MAX) MAX,
static const float g_value_maxs[(int)hrz::Value::_ValueCount] = {HRZ_VALUES};
#undef HRZ_DEFINE_VALUE

void set_value(Value value, float v)
{
    g_value_values[(int)value] = v;
}

float get_value(Value value)
{
    return g_value_values[(int)value];
}

float* get_value_ptr(Value value)
{
    return &g_value_values[(int)value];
}

float get_value_min(Value value)
{
    return g_value_mins[(int)value];
}

float get_value_max(Value value)
{
    return g_value_maxs[(int)value];
}

} // namespace hrz
