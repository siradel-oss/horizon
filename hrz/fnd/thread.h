// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <mutex>
#include <shared_mutex>
#include <thread>

#define HRZ_SCOPED_LOCK_CONCAT_(prefix, suffix) prefix##suffix
#define HRZ_SCOPED_LOCK_CONCAT(prefix, suffix) HRZ_SCOPED_LOCK_CONCAT_(prefix, suffix)

#define HRZ_SCOPED_LOCK(MUTEX) \
    const std::unique_lock<std::mutex> HRZ_SCOPED_LOCK_CONCAT(_lock_, __COUNTER__)(MUTEX)

#define HRZ_SCOPED_EXCLUSIVE_LOCK(MUTEX) \
    const std::unique_lock<std::shared_mutex> HRZ_SCOPED_LOCK_CONCAT(_lock_, __COUNTER__)(MUTEX)
#define HRZ_SCOPED_SHARED_LOCK(MUTEX) \
    const std::shared_lock<std::shared_mutex> HRZ_SCOPED_LOCK_CONCAT(_lock_, __COUNTER__)(MUTEX)

namespace hrz
{

void pin_thread_to_cpu_core(std::thread& thread, size_t core_id);
void pin_current_thread_to_cpu_core(size_t core_id);

enum class ThreadPriority
{
    Low,
    High,
};

void set_thread_priority(std::thread& thread, ThreadPriority priority);
void set_current_thread_priority(ThreadPriority priority);

} // namespace hrz
