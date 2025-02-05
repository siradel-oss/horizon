#include "hrz_core_backend.h"

#include "hrz_core.h"

namespace hrz_core
{
Backend::Backend(void* wsi_instance, void* wsi_window, const hrz_proto::ViewerOptions& options) :
    _init_status(hrz_proto::ViewerInitStatus::INIT_SUCCESS)
{
    std::string serialized_options;
    options.SerializeToString(&serialized_options);
    assert(serialized_options.size() >= 0 && serialized_options.size() < INT_MAX);

    const char* args_data = serialized_options.c_str();
    int args_data_size = (int)serialized_options.size();

    _init_status = (hrz_proto::ViewerInitStatus)hrz_init(
        wsi_instance, wsi_window, args_data, args_data_size, "");
}

bool Backend::frame()
{
    return hrz_frame() != 0;
}

void Backend::cleanup()
{
    hrz_cleanup();
}

std::vector<uint8_t> Backend::rpc(uint32_t service, uint32_t method, const void* data, size_t size)
{
    int output_size;
    char* output_data;

    assert(size < INT_MAX);

    hrz_rpc(service, method, (const char*)data, (int)size, &output_data, &output_size);

    std::vector<uint8_t> output(output_size);
    std::copy((uint8_t*)output_data, (uint8_t*)output_data + output_size, std::begin(output));

    hrz_free_rpc(output_data);

    return output;
}
} // namespace hrz_core
