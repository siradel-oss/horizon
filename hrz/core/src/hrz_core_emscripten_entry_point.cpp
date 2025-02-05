#include "hrz_core.h"

#include <hrz_protocol_all.h>

#include <emscripten.h>
#include <emscripten/bind.h>

#include <iostream>
#include <vector>

using namespace emscripten;

namespace
{
struct Buffer
{
    intptr_t data;
    int size;
};

bool initialized = false;
bool init_success = false;

void loop()
{
    if (init_success)
    {
        if (!hrz_frame())
        {
            hrz_cleanup();
        }
    }
}

unsigned int em_hrz_init(Buffer args, std::string canvas_selector)
{
    if (initialized)
    {
        emscripten_log(EM_LOG_ERROR, "Error: Horizon is already initialised");
        return (unsigned int)hrz_proto::ViewerInitStatus::ALREADY_INITIALIZED;
    }

    unsigned int init_status =
        hrz_init(nullptr, nullptr, (char*)args.data, args.size, canvas_selector.c_str());
    initialized = true;
    init_success = init_status == (unsigned int)hrz_proto::ViewerInitStatus::INIT_SUCCESS;

    if (!init_success)
    {
        emscripten_log(EM_LOG_ERROR, "Error: Horizon failed to initialise");
        hrz_cleanup();
        emscripten_cancel_main_loop();
    }

    return init_status;
}

Buffer em_hrz_rpc(uint32_t service, uint32_t method, Buffer input)
{
    Buffer output;
    hrz_rpc(service, method, (char*)input.data, input.size, (char**)&output.data, &output.size);
    return output;
}

void em_hrz_free_rpc(intptr_t ptr)
{
    hrz_free_rpc((char*)ptr);
}

} // namespace

EMSCRIPTEN_BINDINGS(horizon)
{
    value_object<Buffer>("Buffer").field("data", &Buffer::data).field("size", &Buffer::size);

    function("hrz_init", &em_hrz_init);
    function("hrz_rpc", &em_hrz_rpc);
    function("hrz_free_rpc", &em_hrz_free_rpc);
}

int main(int argc, char* argv[])
{
    emscripten_set_main_loop(loop, -1, 0);
    return 0;
}
