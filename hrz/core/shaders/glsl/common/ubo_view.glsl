// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

// cc means "camera-centered"
layout(std140) uniform View
{
    // This is the proj * view matrix starting after the view transform
    // of the main view.
    mat4 pv_from_main_view;
    mat4 pv_cc;
} hrz_view;
