#pragma once

#include "hrz/api/api.h"
#include "hrz/fnd/defines.h"
#include "hrz/protocol/all.h"

#include <memory>

namespace hrz_core
{
class HRZ_API Backend : public hrz_api::Backend
{
public:
    static std::shared_ptr<Backend> create(
        void* wsi_instance,
        void* wsi_window,
        const hrz_proto::ViewerOptions& options)
    {
        return std::shared_ptr<Backend>(new Backend(wsi_instance, wsi_window, options));
    }

    hrz_proto::ViewerInitStatus init_status() const { return _init_status; }

    bool frame();

    void cleanup();

    std::vector<uint8_t> rpc(uint32_t service, uint32_t method, const void* data, size_t size)
        override;

private:
    Backend(void* wsi_instance, void* wsi_window, const hrz_proto::ViewerOptions& options);

    hrz_proto::ViewerInitStatus _init_status;
};

} // namespace hrz_core
