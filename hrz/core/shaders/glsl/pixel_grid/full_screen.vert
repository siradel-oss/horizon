// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

layout(location = 0) in vec2 i_pos;

void main()
{
    gl_Position = vec4(i_pos, 0.0, 1.0);
}
