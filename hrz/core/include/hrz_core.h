#pragma once

#include <hrz_fnd_defines.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
    unsigned int HRZ_API hrz_init(
        void* wsi_instance,
        void* wsi_window,
        const char* args_data,
        int args_data_size,
        const char* canvas_selector);
    uint32_t HRZ_API hrz_frame(void);
    void HRZ_API hrz_cleanup(void);

    void HRZ_API hrz_rpc(
        uint32_t service,
        uint32_t method,
        const char* data_in,
        int data_in_size,
        char** data_out,
        int* data_out_size);

    void HRZ_API hrz_free_rpc(char* data);

#ifdef __cplusplus
}
#endif
