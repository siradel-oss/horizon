// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

namespace hrz
{

struct Actor;
struct ActorRunner;
struct AttributionRegistry;
struct BlobAllocator;
struct ClientMessageQueue;
struct JobScheduler;
struct SceneModel;
struct VectorDataLoader;

namespace actor_runner
{

ActorRunner* create(
    bool run_on_main_thread,
    AttributionRegistry*,
    BlobAllocator*,
    ClientMessageQueue*,
    JobScheduler*,
    SceneModel*,
    VectorDataLoader*);
void work(
    ActorRunner*,
    AttributionRegistry*,
    BlobAllocator*,
    ClientMessageQueue*,
    JobScheduler*,
    SceneModel*,
    VectorDataLoader*);
void destroy(ActorRunner*, bool leak);
void add_actor(ActorRunner*, std::unique_ptr<Actor>);
bool is_working(ActorRunner*);

} // namespace actor_runner
} // namespace hrz
