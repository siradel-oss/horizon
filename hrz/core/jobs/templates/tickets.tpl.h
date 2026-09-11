// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/job_scheduler.h"

#include "hrz/common/monitoring_defs.h"

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

namespace hrz_jobs
{

{% for job in jobs %}

/**
 * Ticket for the job {{ job.name }}.
 */
struct {{ job.name|snake_to_pascal }}Ticket
{
    ::hrz::job_scheduler::Ticket ticket = 0;
};

{{ job.documentation|to_documentation_block }}
{{ job.name|snake_to_pascal }}Ticket add_job_{{ job.name }}(::hrz::JobScheduler* scheduler, {{ job.params_type }}&& parameters, const hrz::monitoring::ResourceOwner& owner);

/**
 * Cancel a job and discard the result.
 */
void cancel_job(::hrz::JobScheduler* scheduler, {{ job.name|snake_to_pascal }}Ticket ticket);

/**
 * Return true if the job exists in the scheduler.
 */
bool is_job_valid(::hrz::JobScheduler* scheduler, {{ job.name|snake_to_pascal }}Ticket ticket);

/**
 * Return true if the job is finished, whether is was
 * successful or not.
 */
bool is_job_finished(::hrz::JobScheduler* scheduler, {{ job.name|snake_to_pascal }}Ticket ticket);

/**
 * Return the status of the job.
 */
::hrz::job_scheduler::JobStatus get_job_status(::hrz::JobScheduler* scheduler, {{ job.name|snake_to_pascal }}Ticket ticket);

/**
 * Get the response of the job.
 * The job must be finished before calling this function.
 * This call removes the job from the scheduler.
 */
{{ job.response_type }} get_job_response(::hrz::JobScheduler* scheduler, {{ job.name|snake_to_pascal }}Ticket ticket);

{% endfor %}

} // namespace hrz_jobs
