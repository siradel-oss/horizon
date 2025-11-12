#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

{% for f in protocol.files %}
#include "hrz_core_{{ f.name[4:] }}_scene_path.h"
{% endfor %}
