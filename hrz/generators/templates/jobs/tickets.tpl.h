#pragma once

#include "hrz_jobs_protocol.h"

#include "hrz_core_job_scheduler.h"

#include <hrz_common_monitoring_defs.h>
#include <hrz_protocol_all.h>

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

/**
 * Ticket for the job {{ v.name }}.
 */
struct {{ v.name|lower|snake_to_pascal }}Ticket
{
    ::hrz::job_scheduler::Ticket ticket = 0;
};

{{ v.documentation|to_documentation_block }}
{{ v.name|lower|snake_to_pascal }}Ticket add_job_{{ v.name|lower }}(::hrz::JobScheduler* scheduler, {{ v.params_type }}& parameters, const hrz::monitoring::ResourceOwner& owner);

/**
 * Cancel a job and discard the result.
 */
void cancel_job(::hrz::JobScheduler* scheduler, {{ v.name|lower|snake_to_pascal }}Ticket ticket);

/**
 * Return true if the job exists in the scheduler.
 */
bool is_job_valid(::hrz::JobScheduler* scheduler, {{ v.name|lower|snake_to_pascal }}Ticket ticket);

/**
 * Return true if the job is finished, whether is was
 * successful or not.
 */
bool is_job_finished(::hrz::JobScheduler* scheduler, {{ v.name|lower|snake_to_pascal }}Ticket ticket);

/**
 * Return the status of the job.
 */
::hrz::job_scheduler::JobStatus get_job_status(::hrz::JobScheduler* scheduler, {{ v.name|lower|snake_to_pascal }}Ticket ticket);

/**
 * Get the response of the job.
 * The job must be finished before calling this function.
 * This call removes the job from the scheduler.
 */
void get_job_response(::hrz::JobScheduler* scheduler, {{ v.name|lower|snake_to_pascal }}Ticket ticket, {{ v.response_type }}& reponse);

{% endfor %}

{% endif %}
{% endfor %}

}
