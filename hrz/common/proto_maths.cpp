// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/proto_maths.h"

#include "hrz/common/maths.h"

namespace hrz
{

lm::dmat4 to_lm(const hrz_proto::CoordinatesFrame& frame)
{
    lm::vec3 y = to_lm(frame.front());
    lm::vec3 z = to_lm(frame.up());
    return make_frame_transform(y, z, frame.handedness() == hrz_proto::Handedness::RIGHT);
}

lm::dmat4 to_lm(const hrz_proto::Transform& transform)
{
    return lm::translation(lm::dvec3(to_lm(transform.offset())))
        * lm::rotation_normalized(to_lm(transform.rotation(), true))
        * lm::scaling(lm::dvec3(to_lm(transform.scale()))) * to_lm(transform.frame());
}

} // namespace hrz
