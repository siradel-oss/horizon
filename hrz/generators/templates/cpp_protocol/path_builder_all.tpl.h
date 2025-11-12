#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

{% for f in protocol.files %}
#include "hrz_protocol_{{ f.name[4:] }}_path_builder.h"
{% endfor %}
