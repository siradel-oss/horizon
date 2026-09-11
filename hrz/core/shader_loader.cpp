// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/shader_loader.h"

#include <cassert>

void collect_present_shaders(hrz::GpuResourceContext*);

namespace hrz
{
namespace camera_height
{

void collect_shaders(hrz::GpuResourceContext*);

} // namespace camera_height

namespace editor
{

void collect_shaders(hrz::GpuResourceContext*);

} // namespace editor

namespace gizmo_layers
{

void collect_shaders(hrz::GpuResourceContext* rc);

} // namespace gizmo_layers

namespace grid
{

void collect_shaders(hrz::GpuResourceContext* rc);

} // namespace grid

namespace model
{

void collect_shaders(hrz::GpuResourceContext*);

} // namespace model

namespace planet
{

void collect_shaders(hrz::GpuResourceContext*);

} // namespace planet

namespace scene
{

void collect_shaders(hrz::GpuResourceContext*);

} // namespace scene

namespace sky
{

void collect_shaders(hrz::GpuResourceContext*);

} // namespace sky

namespace viewsheds
{

void collect_shaders(hrz::GpuResourceContext*);

} // namespace viewsheds

namespace heatmaps
{

void collect_shaders(hrz::GpuResourceContext*);

}

namespace vt
{

void collect_cylinder_shaders(hrz::GpuResourceContext*);
void collect_extruded_shaders(hrz::GpuResourceContext*);
void collect_flat_overlay_point_shaders(hrz::GpuResourceContext*);
void collect_flat_overlay_polyline_shaders(hrz::GpuResourceContext*);
void collect_flat_overlay_polygon_shaders(hrz::GpuResourceContext*);
void collect_heatmap_shaders(hrz::GpuResourceContext*);
void collect_symbol_shaders(hrz::GpuResourceContext*);

void collect_shaders(hrz::GpuResourceContext* rc)
{
    collect_cylinder_shaders(rc);
    collect_extruded_shaders(rc);
    collect_flat_overlay_point_shaders(rc);
    collect_flat_overlay_polyline_shaders(rc);
    collect_flat_overlay_polygon_shaders(rc);
    collect_heatmap_shaders(rc);
    collect_symbol_shaders(rc);
}

} // namespace vt

namespace point_cloud
{

void collect_shaders(hrz::GpuResourceContext*);

}
} // namespace hrz

namespace hrz
{
namespace shaders
{

void collect_all_shaders(hrz::GpuResourceContext* rc)
{
    assert(rc);

    // Initial shaders.
    collect_present_shaders(rc);
    camera_height::collect_shaders(rc);
    planet::collect_shaders(rc);
    scene::collect_shaders(rc);
    sky::collect_shaders(rc);
    viewsheds::collect_shaders(rc);

    editor::collect_shaders(rc);
    gizmo_layers::collect_shaders(rc);
    grid::collect_shaders(rc);
    model::collect_shaders(rc);
    vt::collect_shaders(rc);
    heatmaps::collect_shaders(rc);
    point_cloud::collect_shaders(rc);
}

} // namespace shaders
} // namespace hrz
