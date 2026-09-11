// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#pragma once

namespace hrz
{

struct CameraViewInfo;
struct CameraHeightSystem;
struct Render;
struct RenderView;

namespace planet
{

struct GeometryResources;

}

namespace camera_height
{

/**
 * Create a new camera height system.
 * This system allow populating a 1×1 texture on the GPU,
 * that contains the distance from the camera to the ground
 * directly below it.
 *
 * All objects with the render type `RenderDepth` are drawn
 * and contribute to the height computation.
 * To avoid unnecessary render calls, the terrain is not
 * drawn, but instead the DTM is sampled directly.
 */
CameraHeightSystem* create_system();

void destroy_system(CameraHeightSystem*, Render*);

void init_render(CameraHeightSystem*, RenderView*);

void register_view(CameraHeightSystem*, Render*);

const char* get_target_name(const CameraHeightSystem*);

void update(CameraHeightSystem*, const CameraViewInfo&);

void draw(CameraHeightSystem*, Render*, const planet::GeometryResources&);

float get_last_downloaded_height(CameraHeightSystem*);

} // namespace camera_height
} // namespace hrz
