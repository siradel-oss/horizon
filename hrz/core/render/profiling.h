#pragma once

#include "hrz/core/render_request.h"

#include <mycelium/backend.h>

#include <optional>
#include <string>
#include <vector>

namespace hrz::render::profiling
{

uint64_t acquire_time_query_id();
void release_time_query_id(uint64_t id);

void enable_profiling(bool enabled);
bool is_enabled();

void new_frame_start_point();
void new_view_start_point();
void register_frame_time_query(uint64_t id);
void set_render_requests(const RenderRequest&);

struct GpuProfilingData
{
    std::vector<std::vector<std::pair<std::string, float>>> pass_durations;
    RenderRequest render_request;
};

std::optional<GpuProfilingData> get_frame_profile(my::Instance*);

void clear(my::Instance*);

} // namespace hrz::render::profiling
