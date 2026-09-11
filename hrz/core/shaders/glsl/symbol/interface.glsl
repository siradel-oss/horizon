// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#ifdef SYMBOL_VISUAL
flat varying uvec2 v_feature_id;
#endif

#ifdef SYMBOL_PICKING
flat varying uint v_feature_index;
#endif
