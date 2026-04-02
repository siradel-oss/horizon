#include "hrz/api/api.h"

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

{% for f in protocol.files %}
#include "{{ f.name }}.pb.h"
{% endfor %}

{% for s in protocol.services %}
{% for m in s.methods %}
void hrz_api::{{ s.full_name|last(".") }}::{{ m.name|snake_case}}(
    const ::{{ m.input|rejoin(".", "::") }}& input,
    ::{{ m.output|rejoin(".", "::") }}& output)
{
    _backend->rpc_msg(0x{{ '%0x' % s.id }}, 0x{{ '%0x' % m.id }}, input, output);
}

{% endfor %}
{% endfor %}

void hrz_api::Backend::rpc_msg(
    uint32_t service,
    uint32_t method,
    const google::protobuf::MessageLite& input,
    google::protobuf::MessageLite& output)
{
    std::string input_str = input.SerializeAsString();

    assert(input_str.size() < INT_MAX);

    std::vector<uint8_t> output_buffer = rpc(service, method, input_str.data(), input_str.size());
    (void)output.ParseFromArray(output_buffer.data(), output_buffer.size());
}
