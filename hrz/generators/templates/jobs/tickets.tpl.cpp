#include "hrz/core/jobs/jobs_tickets.h"

#include "hrz/common/job_params.h"
#include "hrz/common/monitoring_defs.h"
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

{% for job in jobs %}

{{ job.name|snake_to_pascal }}Ticket add_job_{{ job.name }}(::hrz::JobScheduler* scheduler, {{ job.params_type }}& parameters, const hrz::monitoring::ResourceOwner& owner)
{
    auto parameters_any = std::make_any<{{ job.params_type }}>(std::move(parameters));

    {{ job.name|snake_to_pascal }}Ticket ticket;
    ticket.ticket = ::hrz::job_scheduler::add_job(scheduler, JobType::{{ job.name|upper }}, parameters_any, owner);

    return ticket;
}

void cancel_job(::hrz::JobScheduler* scheduler, {{ job.name|snake_to_pascal }}Ticket ticket)
{
    ::hrz::job_scheduler::cancel_job(scheduler, ticket.ticket);
}

bool is_job_valid(::hrz::JobScheduler* scheduler, {{ job.name|snake_to_pascal }}Ticket ticket)
{
    return ::hrz::job_scheduler::is_job_valid(scheduler, ticket.ticket);
}

bool is_job_finished(::hrz::JobScheduler* scheduler, {{ job.name|snake_to_pascal }}Ticket ticket)
{
    return ::hrz::job_scheduler::is_job_finished(scheduler, ticket.ticket);
}

::hrz::job_scheduler::JobStatus get_job_status(::hrz::JobScheduler* scheduler, {{ job.name|snake_to_pascal }}Ticket ticket)
{
    return ::hrz::job_scheduler::get_job_status(scheduler, ticket.ticket);
}

void get_job_response(::hrz::JobScheduler* scheduler, {{ job.name|snake_to_pascal }}Ticket ticket, {{ job.response_type }}& response)
{
    std::any response_any = ::hrz::job_scheduler::get_job_response(scheduler, ticket.ticket);
    auto cast_response = std::any_cast<{{ job.response_type }}>(std::move(response_any));
    std::swap(response, cast_response);
}

{% endfor %}

}
