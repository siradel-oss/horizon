#pragma once

#include "hrz/fnd/bitset.h"
#include "hrz/protocol/scene/index.pb.h"

namespace hrz
{

static constexpr size_t SCENE_VIEW_COUNT = (size_t)hrz_proto::SceneViewIndex_ARRAYSIZE;

using SceneViewBitset = Bitset32<SCENE_VIEW_COUNT>;

} // namespace hrz
