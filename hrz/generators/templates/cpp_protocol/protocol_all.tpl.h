#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "hrz/protocol/services.h"
{% for f in files %}
#include "hrz/protocol/{{ f.name[13:] }}.pb.h"
{% endfor %}
