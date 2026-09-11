// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/core/camera/types.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/protocol/scene/index.pb.h"

#include <mycelium/backend.h>
#include <mycelium/render_graph.h>
#include <mycelium/renderer.h>

namespace hrz
{

// Contains all structs necessary for rendering that are not view-dependent.
struct Render
{
    my::Instance* my;
    GpuResourceContext* rc;
    my::Renderer* rd;
    my::ResourceBinder* rb;
    my::Renderer::ViewMask main_views;
};

struct RenderView : public Render
{
    my::RenderGraph* rg;

    RenderView(const Render& r, my::RenderGraph* rg) : Render(r), rg(rg) {}
};

struct SceneViewRenderGraphUserData
{
    my::Renderer::ViewId main_view;
    hrz_proto::SceneViewIndex scene_view;
};

struct RenderViewInfo
{
    hrz_proto::SceneViewIndex view;
    CameraViewInfo cam_view_info;
    my::Renderer::ViewId view_main;
    my::Renderer::ViewMask all_views;
    double height_above_terrain;
    double perceived_distance;
};

} // namespace hrz
