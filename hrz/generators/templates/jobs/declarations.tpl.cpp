#include "hrz_jobs_declarations.h"

#include <hrz_common_job_params.h>

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

namespace hrz_jobs
{

{% set ns = namespace(params_message=null, response_message=null, params_message_type="", response_message_type="") %}

{% for e in protocol.enums %}

{% if e["full_name"] == "HrzJobsProtocol.JobType" %}


{% for v in e["values"] %}

namespace {{ v.name|lower }}
{

hrz::JobResult run(
    const std::any& params,
    std::any& response,
    const JobContext& context)
{
    const {{ v.params_type }}* typed_params = std::any_cast<{{ v.params_type }}>(&params);

    if (typed_params != nullptr)
    {
        response = std::make_any<{{ v.response_type }}>();
        {{ v.response_type }}& typed_response = std::any_cast<{{ v.response_type }}&>(response);
        hrz::JobResult result = run(*typed_params, typed_response, context);
        return result;
    }
    else
    {
        return hrz::JobResult::FAILURE;
    }
}

}

{% endfor %}

JobFunctionPtr get_job_function(HrzJobsProtocol::JobType job_type)
{
    switch (job_type)
    {
        {% for v in e["values"] %}
        case HrzJobsProtocol::JobType::{{ v["name"] }}:
            return {{ v["name"]|lower }}::run;
        {% endfor %}
        default:
            // @Todo Log?
            return nullptr;
    }
}

{% endif %}
{% endfor %}

}
