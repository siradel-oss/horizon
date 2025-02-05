#pragma once

#include "hrz_jobs_context.h"
#include "hrz_jobs_protocol.h"

#include <hrz_common_job_result.h>
#include <any>

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

{% for e in protocol.enums %}
{% if e["full_name"] == "HrzJobsProtocol.JobType" %}
{% for v in e["values"] %}
{{ v.params_type|to_cpp_forward_declaration }}
{{ v.response_type|to_cpp_forward_declaration }}
{% endfor %}
{% endif %}
{% endfor %}

namespace hrz_jobs
{
using JobFunctionPtr = hrz::JobResult (*)(const std::any&, std::any&, const JobContext&);

{% set ns = namespace(params_message=null, response_message=null, params_message_type="", response_message_type="") %}

{% for m in protocol.messages %}
{% if m["full_name"] == "HrzJobsProtocol.JobParams" %}
{% set ns.params_message = m %}
{% endif %}
{% if m["full_name"] == "HrzJobsProtocol.JobResponse" %}
{% set ns.response_message = m %}
{% endif %}
{% endfor %}

{% for e in protocol.enums %}

{% if e["full_name"] == "HrzJobsProtocol.JobType" %}

{% for v in e["values"] %}

namespace {{ v.name|lower }}
{

{{ v.documentation|to_documentation_block }}
hrz::JobResult run(
    const {{ v.params_type }}& params,
    {{ v.response_type }}& response,
    const JobContext& context);

/**
 * Internal run function for the job {{ v.name }}.
 */
hrz::JobResult run(
    const std::any& params,
    std::any response,
    const JobContext& context);

}

{% endfor %}

JobFunctionPtr get_job_function(HrzJobsProtocol::JobType);

{% endif %}
{% endfor %}

}
