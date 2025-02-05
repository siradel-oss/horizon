#include "hrz_jobs_tickets.h"

#include <hrz_common_job_params.h>
#include <hrz_common_monitoring_defs.h>
#include <any>

#include <cassert>
#include <memory>
#include <google/protobuf/arena.h>

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

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


{{ v.name|lower|snake_to_pascal }}Ticket add_job_{{ v.name|lower }}(::hrz::JobScheduler* scheduler, {{ v.params_type }}& parameters, const hrz::monitoring::ResourceOwner& owner)
{
    auto parameters_any = std::make_any<{{ v.params_type }}>(std::move(parameters));

    {{ v.name|lower|snake_to_pascal }}Ticket ticket;
    ticket.ticket = ::hrz::job_scheduler::add_job(scheduler, hrz_jobs_proto::JobType::{{ v.name }}, parameters_any, owner);

    return ticket;
}

void cancel_job(::hrz::JobScheduler* scheduler, {{ v.name|lower|snake_to_pascal }}Ticket ticket)
{
    ::hrz::job_scheduler::cancel_job(scheduler, ticket.ticket);
}

bool is_job_valid(::hrz::JobScheduler* scheduler, {{ v.name|lower|snake_to_pascal }}Ticket ticket)
{
    return ::hrz::job_scheduler::is_job_valid(scheduler, ticket.ticket);
}

bool is_job_finished(::hrz::JobScheduler* scheduler, {{ v.name|lower|snake_to_pascal }}Ticket ticket)
{
    return ::hrz::job_scheduler::is_job_finished(scheduler, ticket.ticket);
}

::hrz::job_scheduler::JobStatus get_job_status(::hrz::JobScheduler* scheduler, {{ v.name|lower|snake_to_pascal }}Ticket ticket)
{
    return ::hrz::job_scheduler::get_job_status(scheduler, ticket.ticket);
}

void get_job_response(::hrz::JobScheduler* scheduler, {{ v.name|lower|snake_to_pascal }}Ticket ticket, {{ v.response_type }}& response)
{
    std::any response_any = ::hrz::job_scheduler::get_job_response(scheduler, ticket.ticket);
    auto cast_response = std::any_cast<{{ v.response_type }}>(std::move(response_any));
    std::swap(response, cast_response);
}

{% endfor %}

{% endif %}
{% endfor %}

}
