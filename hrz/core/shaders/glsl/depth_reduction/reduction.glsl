// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

void filter_value(inout float a, inout float b, in float f)
{
    if (a == f) { a = b; }
    if (b == f) { b = a; }
}
