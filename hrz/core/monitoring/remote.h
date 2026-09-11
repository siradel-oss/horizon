// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <string_view>

namespace hrz
{

class Monitoring;
struct RemoteMonitoring;
struct BlobAllocator;
struct Event;
struct ClientMessageQueue;
struct LayersInfo;

namespace monitoring
{

RemoteMonitoring* create_remote_monitoring();
void destroy(RemoteMonitoring*);

void work(
    RemoteMonitoring*,
    ClientMessageQueue*,
    const Monitoring*,
    const BlobAllocator*,
    const LayersInfo*);

void set_monitoring_server_address(RemoteMonitoring*, std::string_view address);

bool is_connected(const RemoteMonitoring*);

using ConnectionCallback = std::function<void(bool success)>;
void try_connect(RemoteMonitoring*, const ConnectionCallback& callback = {});
void disconnect(RemoteMonitoring*);

void schedule_gpu_snapshot(RemoteMonitoring*);
void schedule_blob_allocator_snapshot(RemoteMonitoring*);

void set_message_queue_sending_enabled(RemoteMonitoring*, bool enabled);

} // namespace monitoring
} // namespace hrz
