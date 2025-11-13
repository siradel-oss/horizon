#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

{% for f in protocol.files %}
#include "hrz/core/scene_path/{{ f.name[13:] }}_paths.h"
{% endfor %}
