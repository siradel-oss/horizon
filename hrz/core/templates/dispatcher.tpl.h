// SPDX-FileCopyrightText: Copyright 2018 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include "hrz/protocol/services.h"

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

namespace hrz
{

struct RpcImplementations
{
    {% for s in services %}
    std::unique_ptr<::hrz_proto::I{{ s.full_name|last(".") }}> {{ s.full_name|last(".")|snake_case }};
    {% endfor %}
};

void set_rpc_dispatcher_implementations(RpcImplementations&& impls);

}
