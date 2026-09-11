// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_array.h"
#include "hrz/common/maths.h"
#include "hrz/common/vector_tiles/symbol_anchor_data.h"
#include "hrz/core/render_request.h"
#include "hrz/core/scene_view_bitset.h"
#include "hrz/fnd/inlined_vector.h"

#include <lin_maths.h>
#include <mycelium/backend.h>

namespace hrz
{

struct SymbolCullingSystem;
struct Render;
struct JobScheduler;
struct RenderViewInfo;

namespace symbol_culling
{

struct GroupInfo
{
    hrz::BSphere<double> culling_bsphere;
    lm::dvec3 group_origin;
    uint16_t representation_z_index;

    hrz::InlinedVector<vt::AnchorPrototype, 8> anchors;
};

struct GroupHandle
{
    uint64_t o;
};

SymbolCullingSystem* create();
void destroy(SymbolCullingSystem*);

RenderRequest work(SymbolCullingSystem*, JobScheduler*, std::span<const RenderViewInfo> views_info);
void work_gpu(SymbolCullingSystem*, Render* render);
void draw(SymbolCullingSystem*, const RenderRequest&);

bool is_working(SymbolCullingSystem*);

GroupHandle register_group(
    SymbolCullingSystem*,
    GroupInfo&& group_info,
    hrz::BlobArray<vt::AnchorSpan> anchor_spans,
    hrz::BlobArray<vt::AnchorCullingInfo> anchors,
    bool ignore_occlusions,
    const hrz::monitoring::ResourceOwner& resource_owner,
    std::initializer_list<std::pair<hrz::MetadataString, hrz::MetadataString>> metadata);

std::array<my::ResourceHandle, SCENE_VIEW_COUNT> initialize_visibility_textures(
    SymbolCullingSystem* sys,
    GroupHandle handle,
    Render* render);

void unregister_group(SymbolCullingSystem*, GroupHandle);

void schedule_draw_group(
    SymbolCullingSystem* sys,
    GroupHandle handle,
    SceneViewBitset views_visibility);

uint64_t get_last_culled_frame(SymbolCullingSystem*);

} // namespace symbol_culling
} // namespace hrz
