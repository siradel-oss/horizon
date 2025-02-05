#include <ws_server.h>

#include <stdio.h>

// https://stackoverflow.com/a/28827188
#ifdef _WIN32
#    include <windows.h>
#elif _POSIX_C_SOURCE >= 199309L
#    include <time.h> // for nanosleep
#else
#    include <unistd.h> // for usleep
#endif

void sleep_ms(int milliseconds)
{ // cross-platform sleep function
#ifdef _WIN32
    Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#else
    if (milliseconds >= 1000) sleep(milliseconds / 1000);
    usleep((milliseconds % 1000) * 1000);
#endif
}

struct Handler : public ws::ServerHandler
{
    ws::Server* server;

    void on_client_connect() override { printf("Client connected\n"); }

    void on_client_disconnect() override { printf("Client disconnected\n"); }

    void on_raw_message(const void* data, size_t size) override
    {
        printf("Received message of length %zu\n", size);
        ws::send_raw(server, data, size);
    }
};

int main(int argc, char* argv[])
{
    std::pair<ws::Server*, ws::Status> server_status = ws::create_server(8500);
    if (server_status.second != ws::Status::Ok)
    {
        printf("WS error: %s\n", ws::to_string(server_status.second));
        return 1;
    }

    printf("Listening on port 8500\n");

    Handler handler;
    handler.server = server_status.first;

    while (true)
    {
        ws::poll(handler.server, &handler);
        sleep_ms(20);
    }

    return 0;
}
