#include "hrz/core/jobs/jobs_declarations.h"

#include "hrz/core/jobs/all_params_responses.h"

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

namespace hrz_jobs
{

{% for job in jobs %}

namespace {{ job.name }}
{

hrz_jobs::JobResult run(
    const std::any& params,
    std::any& response,
    const JobContext& context)
{
    const {{ job.params_type }}* typed_params = std::any_cast<{{ job.params_type }}>(&params);

    if (typed_params != nullptr)
    {
        response = std::make_any<{{ job.response_type }}>();
        {{ job.response_type }}& typed_response = std::any_cast<{{ job.response_type }}&>(response);
        hrz_jobs::JobResult result = run(*typed_params, typed_response, context);
        return result;
    }
    else
    {
        return hrz_jobs::JobResult::FAILURE;
    }
}

} // namespace {{ job.name }}

{% endfor %}

JobFunctionPtr get_job_function(JobType job_type)
{
    switch (job_type)
    {
        {% for job in jobs %}
        case {{ job.name|upper }}:
            return &{{ job.name }}::run;
        {% endfor %}
        default:
            // @Todo Log?
            return nullptr;
    }
}

} // namespace hrz_jobs
