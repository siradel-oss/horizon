// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

namespace hrz
{

struct GpuResourceContext;

} // namespace hrz

namespace hrz::shaders
{

// Collects all the shaders that systems want to declare. It must be called during the
// initialization of the core. It lets Mycelium know early about the shaders so that it can decide
// internally how to compile and link them.
void collect_all_shaders(hrz::GpuResourceContext*);

} // namespace hrz::shaders
