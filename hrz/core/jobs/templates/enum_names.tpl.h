// SPDX-FileCopyrightText: Copyright 2018 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/core/jobs/jobs_declarations.h"

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

namespace hrz_jobs
{

static const char* job_type_name(JobType value)
{
    switch (value)
    {
        {% for job in jobs %}
        case {{ job.name|upper }}: return "{{ job.name }}";
        {% endfor %}
        default: return "<Unknown>";
    }
}

} // namespace hrz_jobs
