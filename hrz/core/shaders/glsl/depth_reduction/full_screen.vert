// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

in vec2 i_pos;

void main()
{
    gl_Position = vec4(i_pos, 0, 1);
}
