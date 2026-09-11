// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

mat3 compute_normal_matrix(mat3 m)
{
    // https://github.com/graphitemaster/normals_revisited
    return mat3(
        cross(m[1], m[2]),
        cross(m[2], m[0]),
        cross(m[0], m[1])
    );
}
