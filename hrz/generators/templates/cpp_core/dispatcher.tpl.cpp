#include "hrz_core_rpc_dispatcher.h"
#include "hrz_core.h"

#include <google/protobuf/arena.h>
#include <hrz_protocol_all.h>
#include <hrz_common_profiling.h>

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

namespace hrz
{
    static RpcImplementations impls;

    void set_rpc_dispatcher_implementations(RpcImplementations&& new_impls)
    {
        impls = std::move(new_impls);
    }
}

static google::protobuf::Arena arena;

extern "C" void hrz_rpc(
    uint32_t        service,
    uint32_t        method,
    const char*     data_in,
    int             data_in_size,
    char**          data_out,
    int*            data_out_size)
{
    HRZ_SCOPED_SAMPLE("hrz rpc");

    std::string serialized_output;
    *data_out = NULL;
    *data_out_size = 0;

    switch (service)
    {
    {% for s in services %}
    {% set service_name = s.full_name|last(".") %}
    case 0x{{ '%0x' % s.id }}: // {{ s.full_name }}
        switch (method)
        {
        {% for m in s.methods %}
        case 0x{{ '%0x' % m.id }}: // {{ m.name }}
            {
                auto input = google::protobuf::Arena::CreateMessage<::{{ m.input|rejoin(".", "::") }}>(&arena);

                if (input->ParseFromArray(data_in, data_in_size))
                {
                    auto output = google::protobuf::Arena::CreateMessage<::{{ m.output|rejoin(".", "::") }}>(&arena);
                    hrz::impls.{{ service_name|snake_case }}->{{ m.name|snake_case }}(*input, *output);
                    output->SerializeToString(&serialized_output);
                }
            }
            break;
        {% endfor %}
        }
        break;
    {% endfor %}
    }

    if (serialized_output.size() > 0)
    {
        assert(serialized_output.size() < INT_MAX);
        *data_out_size = (int)serialized_output.size();
        *data_out = (char*)malloc(serialized_output.size());
        memcpy(*data_out, serialized_output.c_str(), serialized_output.size());
    }

    arena.Reset();
}

extern "C" void hrz_free_rpc(char* data)
{
    if (data)
    {
        free(data);
    }
}
