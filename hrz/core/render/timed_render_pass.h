// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <mycelium/render_graph.h>

namespace hrz::render
{

class TimedRenderPass : public my::RenderPass
{
    std::string _name;

public:
    explicit TimedRenderPass(const char* name) : _name(name) {}

    void execute(const my::RenderGraph::ExecutionContext& ctx) final;
    virtual void execute_timed(const my::RenderGraph::ExecutionContext&) = 0;
};

} // namespace hrz::render
