// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/fnd/thread.h"

#include "hrz/fnd/log.h"

#if HRZ_LINUX || HRZ_EMSCRIPTEN
#    include <pthread.h>
#elif HRZ_WINDOWS
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#endif

#include <cassert>

namespace hrz
{
namespace
{

void pin_thread_to_cpu_core(const std::thread::native_handle_type& native_handle, size_t core_id)
{
#if HRZ_LINUX
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    int res = pthread_setaffinity_np(native_handle, sizeof(cpu_set_t), &cpuset);
    if (res != 0)
    {
        HRZ_LOG_ERROR("Error when setting thread affinity: {}", res);
    }
#elif HRZ_WINDOWS
    DWORD_PTR mask = 1 << 1;
    DWORD_PTR res = SetThreadAffinityMask(native_handle, mask);
    if (res == 0)
    {
        DWORD error = GetLastError();
        HRZ_LOG_ERROR("Error when setting thread affinity: {}", error);
    }
#endif
}

} // namespace

void pin_thread_to_cpu_core(std::thread& thread, size_t core_id)
{
    pin_thread_to_cpu_core(thread.native_handle(), core_id);
}

void pin_current_thread_to_cpu_core(size_t core_id)
{
#if HRZ_LINUX
    pin_thread_to_cpu_core(pthread_self(), core_id);
#elif HRZ_WINDOWS
    pin_thread_to_cpu_core(::GetCurrentThread(), core_id);
#endif
}

namespace
{

void set_thread_priority(
    const std::thread::native_handle_type& native_handle,
    ThreadPriority priority)
{
#if HRZ_LINUX
    // The regular scheduling policy on Linux, SCHED_OTHER, does
    // not allow setting thread priorities. (Both min and max
    // priority values are 0.)
    // The way priorities can be set is by setting nice values.
    // According to POSIX, the nice value applies to the whole
    // process, however on Linux it can apply to one thread.
    // We could for example set nice values of -5 to high-
    // priority threads and +5 to low-priority threads.
    // But nice values are set through the `setpriority()`
    // function, which takes a Linux tid as argument to
    // identify the thread. And glibc's pthread offers no way
    // to obtain the tid of a thread. We could use `gettid()`
    // but it has to be called from each thread and the results
    // must then be communicated to the main thread. This is
    // not convenient.
    // See https://sourceware.org/bugzilla/show_bug.cgi?id=6399
#elif HRZ_WINDOWS
    int priority_value = 0;
    switch (priority)
    {
        case ThreadPriority::Low: priority_value = THREAD_PRIORITY_BELOW_NORMAL; break;
        case ThreadPriority::High: priority_value = THREAD_PRIORITY_ABOVE_NORMAL; break;
        default: assert(false && "Unhandled case"); break;
    }

    BOOL res = SetThreadPriority(native_handle, priority_value);
    if (res == 0)
    {
        DWORD error = GetLastError();
        HRZ_LOG_ERROR("Error when setting thread priority: {}", error);
    }
#endif
}

} // namespace

void set_thread_priority(std::thread& thread, ThreadPriority priority)
{
    set_thread_priority(thread.native_handle(), priority);
}

void set_current_thread_priority(ThreadPriority priority)
{
#if HRZ_LINUX
    set_thread_priority(pthread_self(), priority);
#elif HRZ_WINDOWS
    set_thread_priority(::GetCurrentThread(), priority);
#endif
}

} // namespace hrz
