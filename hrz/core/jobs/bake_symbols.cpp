#include "hrz/common/profiling.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/symbol/baker.h"

namespace hrz_jobs::bake_symbols
{
hrz_jobs::JobResult run(
    const hrz_jobs::SymbolBakingData& params,
    hrz_jobs::BakedSymbols& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("bake symbols");

    symbol::SymbolBaker baker(params, context);
    return baker.bake(response);
}
} // namespace hrz_jobs::bake_symbols
