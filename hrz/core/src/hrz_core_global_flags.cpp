#include "hrz_core_global_flags.h"

#include <hrz_fnd_log.h>

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

} // namespace hrz
