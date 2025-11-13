#pragma once

#include "hrz/core/jobs/context.h"
#include "hrz/core/jobs/jobs_type.h"

#include "hrz/common/job_result.h"
#include <any>

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

{% for job in jobs %}
{{ job.params_type|to_cpp_forward_declaration }}
{{ job.response_type|to_cpp_forward_declaration }}
{% endfor %}

namespace hrz_jobs
{

using JobFunctionPtr = hrz::JobResult (*)(const std::any&, std::any&, const JobContext&);
JobFunctionPtr get_job_function(JobType);

{% for job in jobs %}

namespace {{ job.name }}
{

{{ job.documentation|to_documentation_block }}
hrz::JobResult run(
    const {{ job.params_type }}& params,
    {{ job.response_type }}& response,
    const JobContext& context);

/**
 * Internal run function for the job {{ job.name }}.
 */
hrz::JobResult run(
    const std::any& params,
    std::any response,
    const JobContext& context);

} // namespace {{ job.name }}

{% endfor %}

} // namespace hrz_jobs
