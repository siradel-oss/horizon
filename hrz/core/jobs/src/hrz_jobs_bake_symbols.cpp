#include "hrz_jobs_declarations.h"
#include "symbol/hrz_jobs_symbol_baker.h"

#include <hrz_common_profiling.h>

namespace hrz_jobs::bake_symbols
{
hrz::JobResult run(
    const hrz::vt::SymbolBakingData& params,
    hrz::vt::BakedSymbols& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("bake symbols");

    symbol::SymbolBaker baker(params, context);
    return baker.bake(response);
}
} // namespace hrz_jobs::bake_symbols
