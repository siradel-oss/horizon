#pragma once

namespace hrz
{
struct BlobAllocator;
struct JobScheduler;

enum class ActorStatus
{
    WORKING,
    IDLE,
    RELEASED,
};

struct Actor
{
    virtual ~Actor() = default;
    virtual ActorStatus work_async(BlobAllocator*, JobScheduler*) = 0;
};
} // namespace hrz
