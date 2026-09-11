// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/backend.h"

#include "hrz/core/core.h"

// Reexport those to the dynamic library.

unsigned int hrz_core_init(
    void* wsi_instance,
    void* wsi_window,
    const char* args_data,
    int args_data_size,
    const char* canvas_selector);

uint32_t hrz_core_frame();

void hrz_core_cleanup();

void hrz_core_rpc(
    uint32_t service,
    uint32_t method,
    const char* data_in,
    int data_in_size,
    char** data_out,
    int* data_out_size);

void hrz_core_free_rpc(char* data);

extern "C" unsigned int hrz_init(
    void* wsi_instance,
    void* wsi_window,
    const char* args_data,
    int args_data_size,
    const char* canvas_selector)
{
    return hrz_core_init(wsi_instance, wsi_window, args_data, args_data_size, canvas_selector);
}

extern "C" uint32_t hrz_frame(void)
{
    return hrz_core_frame();
}

extern "C" void hrz_cleanup(void)
{
    return hrz_core_cleanup();
}

extern "C" void hrz_rpc(
    uint32_t service,
    uint32_t method,
    const char* data_in,
    int data_in_size,
    char** data_out,
    int* data_out_size)
{
    return hrz_core_rpc(service, method, data_in, data_in_size, data_out, data_out_size);
}

extern "C" void hrz_free_rpc(char* data)
{
    return hrz_core_free_rpc(data);
}

namespace hrz_core
{

Backend::Backend(void* wsi_instance, void* wsi_window, const hrz_proto::ViewerOptions& options) :
    _init_status(hrz_proto::ViewerInitStatus::INIT_SUCCESS)
{
    std::string serialized_options;
    (void)options.SerializeToString(&serialized_options);
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
