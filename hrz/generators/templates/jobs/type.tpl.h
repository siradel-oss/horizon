#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

namespace hrz_jobs
{

enum JobType
{
{% for job in jobs %}
    {{ job.name|upper }},
{% endfor %}
};

} // namespace hrz_jobs
