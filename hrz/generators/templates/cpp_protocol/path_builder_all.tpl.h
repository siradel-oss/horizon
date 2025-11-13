#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

{% for f in protocol.files %}
#include "hrz/protocol/path_builder/{{ f.name[13:] }}.h"
{% endfor %}
