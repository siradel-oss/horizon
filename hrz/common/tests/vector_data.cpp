// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/vector_data/attribute_type_owned.h"
#include "hrz/common/vector_data/feature_id.h"

#include <gtest/gtest.h>

using namespace hrz::vector_data;

TEST(FeatureId, same_hash_on_all_platforms)
{
    FeatureId::Builder builder;
    builder.add_value(1, attr_from<OwnedAttributeValue>(1));
    builder.add_value(2, attr_from<OwnedAttributeValue>(2.5));
    builder.add_value(3, attr_from<OwnedAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff)));
    builder.add_value(3, attr_from<OwnedAttributeValue>(INT64_C(0x1fff'ffff'ffff'ffff)));
    builder.add_value(4, attr_from<OwnedAttributeValue>("ezhfozijfiozjf"));
    FeatureId id = builder.build();
    EXPECT_EQ(id.hash(), UINT64_C(14593653686077784397));
}

TEST(FeatureId, build_out_of_order)
{
    FeatureId::Builder builder;
    builder.add_value(2, attr_from<OwnedAttributeValue>(2.5));
    builder.add_value(1, attr_from<OwnedAttributeValue>(1));
    builder.add_value(3, attr_from<OwnedAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff)));
    builder.add_value(4, attr_from<OwnedAttributeValue>("ezhfozijfiozjf"));
    FeatureId id = builder.build();

    FeatureId::Builder builder2;
    builder2.add_value(4, attr_from<OwnedAttributeValue>("ezhfozijfiozjf"));
    builder2.add_value(1, attr_from<OwnedAttributeValue>(1));
    builder2.add_value(2, attr_from<OwnedAttributeValue>(2.5));
    builder2.add_value(3, attr_from<OwnedAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff)));
    FeatureId id2 = builder2.build();

    EXPECT_EQ(id.hash(), id2.hash());
}
