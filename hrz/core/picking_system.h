// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/picking_types.h"
#include "hrz/core/render_request.h"

#include <lin_maths.h>
#include <mycelium/render_graph.h>

#include <cstdint>
#include <span>
#include <vector>

namespace hrz
{

struct CameraHeightSystem;
struct VectorFlatOverlaySystem;
struct OverlayCamerasInfo;
struct Render;
struct RenderView;
struct HeatmapSystem;

/**
 * This system triggers a render to the picking framebuffer
 * when invoked.
 * It reads the values at mouse position afterwards.
 */
struct PickingSystem;

namespace picking
{

/**
 * Create a picking system.
 */
PickingSystem* create_system();

/**
 * Initializes the picking system for rendering.
 */
my::RenderPassId initialize_rendering(
    PickingSystem*,
    const CameraHeightSystem*,
    const VectorFlatOverlaySystem*,
    uint32_t flat_overlay_texture_size,
    RenderView*);

/**
 * Sets the projection and view matrices used for this frame.
 * This is used to decode the picking position once the results are
 * available.
 */
void set_matrices_for_this_frame(
    PickingSystem*,
    const lm::dmat4& proj,
    const lm::dmat4& view,
    const hrz::OverlayCamerasInfo&);

/**
 * Attempts to retrieve all picking results from previous frames.
 */
void retrieve_results(PickingSystem*, HeatmapSystem*, Render*);

/**
 * This enqueues all picking requests that happened during the frame and cleans
 * up the internal state before a new frame begins.
 * It should be called at the end of a frame, after dispatching the draw calls
 * responsible for drawing stuff that should be pickable.
 */
void end_frame(PickingSystem*);

/**
 * Destroy the picking system passed as parameter.
 */
void destroy_system(PickingSystem*, Render*);

/**
 * Tell the picking system to trigger a render to the picking
 * framebuffer, and to sample it at the given position on the
 * viewport. The returned ticket will be used to retrieve the result.
 */
PositionTicket schedule_pick(
    PickingSystem*,
    lm::ivec2 viewport_sample_position,
    std::span<const hrz_proto::LayerHandle> included_rasters = {});

/**
 * Tell the picking system to trigger a render to the picking framebuffer, and
 * to sample it inside the rectangle given as argument. The returned ticket
 * will be used to retrieve the result.
 */
AreaTicket schedule_pick(PickingSystem*, lm::ibbox2 viewport_sample_rectangle);

/**
 * Should be called once per frame.
 */
RenderRequest work(PickingSystem*);

/**
 * Retrieves the result from a given ticket.
 * Returns whether the result was ready or not.
 * After calling this, the ticket is invalidated if the function
 * returned true.
 */
bool retrieve_result(PickingSystem*, PositionTicket ticket, PositionResult* result);

/**
 * Retrieves the result from a given ticket.
 * Returns whether the result was ready or not.
 * After calling this, the ticket is invalidated if the function
 * returned true.
 * The results are sorted by system id, then complementary id, then object id.
 */
bool retrieve_result(PickingSystem*, AreaTicket ticket, std::vector<AreaResult>& result);

/**
 * Cancels a picking request.
 */
void cancel(PickingSystem*, PositionTicket ticket);
void cancel(PickingSystem*, AreaTicket ticket);

} // namespace picking

} // namespace hrz
