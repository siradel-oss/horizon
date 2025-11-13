#include "hrz/common/attributes.h"

#include <gtest/gtest.h>

using namespace hrz;
using namespace vector_data;

TEST(AttributeValueConstruction, null_value)
{
    OwnedAttributeValue attr = attr_null<OwnedAttributeValue>();
    EXPECT_TRUE(attr_is_null(attr));
}

TEST(AttributeValueConstruction, empty_string)
{
    auto attr = attr_empty_string<OwnedAttributeValue>();
    EXPECT_TRUE(attr_is_string(attr));
    EXPECT_EQ(attr_get_string(attr), "");
}

TEST(AttributeValueConstruction, from_bool)
{
    auto attr = attr_from<OwnedAttributeValue>(true);
    EXPECT_TRUE(attr_is_bool(attr));
    EXPECT_EQ(attr_get_bool(attr), true);

    attr = attr_from<OwnedAttributeValue>(false);
    EXPECT_TRUE(attr_is_bool(attr));
    EXPECT_EQ(attr_get_bool(attr), false);
}

TEST(AttributeValueConstruction, from_double)
{
    auto attr = attr_from<OwnedAttributeValue>(42.0);
    EXPECT_TRUE(attr_is_number(attr));
    EXPECT_EQ(attr_get_number(attr), 42.0);

    attr = attr_from<OwnedAttributeValue>(std::numeric_limits<double>::quiet_NaN());
    EXPECT_TRUE(attr_is_number(attr));
    EXPECT_TRUE(std::isnan(attr_get_number(attr)));

    attr = attr_from<OwnedAttributeValue>(std::numeric_limits<double>::signaling_NaN());
    EXPECT_TRUE(attr_is_number(attr));
    EXPECT_TRUE(std::isnan(attr_get_number(attr)));

    attr = attr_from<OwnedAttributeValue>(std::numeric_limits<double>::infinity());
    EXPECT_TRUE(attr_is_number(attr));
    EXPECT_TRUE(std::isinf(attr_get_number(attr)));
    EXPECT_GT(attr_get_number(attr), 0.0);

    attr = attr_from<OwnedAttributeValue>(std::numeric_limits<double>::infinity() * -1.0);
    EXPECT_TRUE(attr_is_number(attr));
    EXPECT_TRUE(std::isinf(attr_get_number(attr)));
    EXPECT_LT(attr_get_number(attr), 0.0);
}

TEST(AttributeValueConstruction, from_string)
{
    auto attr = attr_from<OwnedAttributeValue>("Hello, World!");
    EXPECT_TRUE(attr_is_string(attr));
    EXPECT_EQ(attr_get_string(attr), "Hello, World!");

    attr = attr_from<OwnedAttributeValue>("");
    EXPECT_TRUE(attr_is_string(attr));
    EXPECT_EQ(attr_get_string(attr), "");
}

TEST(AttributeValueConstruction, from_color)
{
    auto attr = attr_from_color<OwnedAttributeValue>(lm::ubvec4(255, 0, 0, 255));
    EXPECT_TRUE(attr_is_number(attr));
    EXPECT_EQ(attr_get_number(attr), 0xff0000ff);

    attr = attr_from_color<OwnedAttributeValue>(lm::vec4(0.5, 0.0, 0.0, 1.0));
    EXPECT_TRUE(attr_is_number(attr));
    EXPECT_EQ(attr_get_number(attr), 0xff000080);
}

TEST(AttributeValueConstruction, from_uint64)
{
    auto attr = attr_from<OwnedAttributeValue>(UINT64_C(42));
    EXPECT_TRUE(attr_is_number(attr));
    EXPECT_EQ(attr_get_number(attr), 42.0);

    attr = attr_from<OwnedAttributeValue>(INT64_C(1229801703532086340));
    EXPECT_TRUE(attr_is_uint64(attr));
    EXPECT_EQ(attr_get_uint64(attr), UINT64_C(1229801703532086340));
}

TEST(AttributeValueConstruction, from_int64)
{
    auto attr = attr_from<OwnedAttributeValue>(INT64_C(42));
    EXPECT_TRUE(attr_is_number(attr));
    EXPECT_EQ(attr_get_number(attr), 42.0);

    attr = attr_from<OwnedAttributeValue>(INT64_C(-42));
    EXPECT_TRUE(attr_is_number(attr));
    EXPECT_EQ(attr_get_number(attr), -42.0);

    attr = attr_from<OwnedAttributeValue>(INT64_C(1229801703532086340));
    EXPECT_TRUE(attr_is_uint64(attr));
    EXPECT_EQ(attr_get_uint64(attr), UINT64_C(1229801703532086340));

    attr = attr_from<OwnedAttributeValue>(INT64_C(-1229801703532086340));
    EXPECT_TRUE(attr_is_int64(attr));
    EXPECT_EQ(attr_get_int64(attr), INT64_C(-1229801703532086340));
}

using TestPacked = PackedAttributeValue;
using TestPackedTraits = PackedAttributeValueTraits<std::vector<char>>;

TEST(PackedAttributeValueConstruction, from_number)
{
    std::vector<char> blob;

    auto attr1 = attr_from<TestPacked, TestPackedTraits>(42.0, blob);
    auto attr2 =
        attr_from<TestPacked, TestPackedTraits>(std::numeric_limits<double>::quiet_NaN(), blob);
    auto attr3 =
        attr_from<TestPacked, TestPackedTraits>(std::numeric_limits<double>::signaling_NaN(), blob);
    auto attr4 =
        attr_from<TestPacked, TestPackedTraits>(std::numeric_limits<double>::infinity(), blob);
    auto attr5 = attr_from<TestPacked, TestPackedTraits>(
        std::numeric_limits<double>::infinity() * -1.0, blob);

    EXPECT_TRUE(attr_is_number(attr1));
    EXPECT_TRUE(attr_is_number(attr2));
    EXPECT_TRUE(attr_is_number(attr3));
    EXPECT_TRUE(attr_is_number(attr4));
    EXPECT_TRUE(attr_is_number(attr5));

    EXPECT_EQ(attr_get_number(attr1, blob), 42.0);
    EXPECT_TRUE(std::isnan(attr_get_number(attr2, blob)));
    EXPECT_TRUE(std::isnan(attr_get_number(attr3, blob)));
    EXPECT_TRUE(std::isinf(attr_get_number(attr4, blob)));
    EXPECT_GT(attr_get_number(attr4, blob), 0.0);
    EXPECT_TRUE(std::isinf(attr_get_number(attr5, blob)));
    EXPECT_LT(attr_get_number(attr5, blob), 0.0);
}

TEST(PackedAttributeValueConstruction, from_bool)
{
    std::vector<char> blob;

    auto attr1 = attr_from<TestPacked, TestPackedTraits>(true, blob);
    auto attr2 = attr_from<TestPacked, TestPackedTraits>(false, blob);

    EXPECT_TRUE(attr_is_bool(attr1));
    EXPECT_TRUE(attr_is_bool(attr2));

    EXPECT_EQ(attr_get_bool(attr1, blob), true);
    EXPECT_EQ(attr_get_bool(attr2, blob), false);
}

TEST(PackedAttributeValueConstruction, from_uint64)
{
    std::vector<char> blob;

    auto attr1 = attr_from<TestPacked, TestPackedTraits>(UINT64_C(42), blob);
    auto attr2 = attr_from<TestPacked, TestPackedTraits>(UINT64_C(1229801703532086340), blob);

    EXPECT_TRUE(attr_is_number(attr1));
    EXPECT_TRUE(attr_is_uint64(attr2));

    EXPECT_EQ(attr_get_number(attr1, blob), 42.0);
    EXPECT_EQ(attr_get_uint64(attr2, blob), UINT64_C(1229801703532086340));
}

TEST(PackedAttributeValueConstruction, from_int64)
{
    std::vector<char> blob;

    auto attr1 = attr_from<TestPacked, TestPackedTraits>(INT64_C(42), blob);
    auto attr2 = attr_from<TestPacked, TestPackedTraits>(INT64_C(-42), blob);
    auto attr3 = attr_from<TestPacked, TestPackedTraits>(INT64_C(1229801703532086340), blob);
    auto attr4 = attr_from<TestPacked, TestPackedTraits>(INT64_C(-1229801703532086340), blob);

    EXPECT_TRUE(attr_is_number(attr1));
    EXPECT_TRUE(attr_is_number(attr2));
    EXPECT_TRUE(attr_is_uint64(attr3));
    EXPECT_TRUE(attr_is_int64(attr4));

    EXPECT_EQ(attr_get_number(attr1, blob), 42.0);
    EXPECT_EQ(attr_get_number(attr2, blob), -42.0);
    EXPECT_EQ(attr_get_uint64(attr3, blob), UINT64_C(1229801703532086340));
    EXPECT_EQ(attr_get_int64(attr4, blob), INT64_C(-1229801703532086340));
}

TEST(PackedAttributeValueConstruction, from_color)
{
    std::vector<char> blob;

    auto attr1 = attr_from_color<TestPacked, TestPackedTraits>(lm::ubvec4(255, 0, 0, 255), blob);
    auto attr2 = attr_from_color<TestPacked, TestPackedTraits>(lm::vec4(0.5, 0.0, 0.0, 1.0), blob);

    EXPECT_TRUE(attr_is_number(attr1));
    EXPECT_TRUE(attr_is_number(attr2));

    EXPECT_EQ(attr_get_number(attr1, blob), 0xff0000ff);
    EXPECT_EQ(attr_get_number(attr2, blob), 0xff000080);
}

TEST(PackedAttributeValueConstruction, from_string)
{
    std::vector<char> blob;

    auto attr1 = attr_from<TestPacked, TestPackedTraits>("Hello, World!", blob);
    auto attr2 = attr_from<TestPacked, TestPackedTraits>("", blob);
    auto attr3 = attr_from<TestPacked, TestPackedTraits>("ey", blob);

    EXPECT_TRUE(attr_is_string(attr1));
    EXPECT_TRUE(attr_is_string(attr2));
    EXPECT_TRUE(attr_is_string(attr3));

    EXPECT_EQ(attr_get_string(attr1, blob), "Hello, World!");
    EXPECT_EQ(attr_get_string(attr2, blob), "");
    EXPECT_EQ(attr_get_string(attr3, blob), "ey");
}

TEST(PackedAttributeValueConstruction, empty_string)
{
    std::vector<char> blob;
    auto attr = attr_empty_string<TestPacked, TestPackedTraits>();
    EXPECT_TRUE(attr_is_string(attr));
    EXPECT_EQ(attr_get_string(attr, blob), "");
}

TEST(AttributeValueConversion, ref_owned_packed)
{
    std::vector<char> blob;

    auto owned1 = attr_from<OwnedAttributeValue>(42.0);
    auto owned2 = attr_from<OwnedAttributeValue>(true);
    auto owned3 = attr_from<OwnedAttributeValue>(0xffff'ffff'ffff'ffff);
    auto owned4 = attr_from<OwnedAttributeValue>("IT'S METAPROGRAMMING TIME");
    auto owned5 = attr_null<OwnedAttributeValue>();

    auto attr1 = attr_from<TestPacked, TestPackedTraits>(attr_as_ref(owned1), blob);
    auto attr2 = attr_from<TestPacked, TestPackedTraits>(attr_as_ref(owned2), blob);
    auto attr3 = attr_from<TestPacked, TestPackedTraits>(attr_as_ref(owned3), blob);
    auto attr4 = attr_from<TestPacked, TestPackedTraits>(attr_as_ref(owned4), blob);
    auto attr5 = attr_from<TestPacked, TestPackedTraits>(attr_as_ref(owned5), blob);

    EXPECT_TRUE(attr_is_number(attr1));
    EXPECT_TRUE(attr_is_bool(attr2));
    EXPECT_TRUE(attr_is_uint64(attr3));
    EXPECT_TRUE(attr_is_string(attr4));
    EXPECT_TRUE(attr_is_null(attr5));

    EXPECT_EQ(attr_get_number(attr1, blob), 42.0);
    EXPECT_EQ(attr_get_bool(attr2, blob), true);
    EXPECT_EQ(attr_get_uint64(attr3, blob), 0xffff'ffff'ffff'ffff);
    EXPECT_EQ(attr_get_string(attr4, blob), "IT'S METAPROGRAMMING TIME");

    auto ref1 = attr_as_ref(attr1, blob);
    auto ref2 = attr_as_ref(attr2, blob);
    auto ref3 = attr_as_ref(attr3, blob);
    auto ref4 = attr_as_ref(attr4, blob);
    auto ref5 = attr_as_ref(attr5, blob);

    EXPECT_TRUE(attr_is_number(ref1));
    EXPECT_TRUE(attr_is_bool(ref2));
    EXPECT_TRUE(attr_is_uint64(ref3));
    EXPECT_TRUE(attr_is_string(ref4));
    EXPECT_TRUE(attr_is_null(ref5));

    EXPECT_EQ(attr_get_number(ref1), 42.0);
    EXPECT_EQ(attr_get_bool(ref2), true);
    EXPECT_EQ(attr_get_uint64(ref3), 0xffff'ffff'ffff'ffff);
    EXPECT_EQ(attr_get_string(ref4), "IT'S METAPROGRAMMING TIME");
}

TEST(AttributeValueAs, is_null)
{
    EXPECT_TRUE(attr_is_null(attr_null<RefAttributeValue>()));

    EXPECT_FALSE(attr_is_null(attr_from<RefAttributeValue>(true)));
    EXPECT_FALSE(attr_is_null(attr_from<RefAttributeValue>(1.0)));
    EXPECT_FALSE(
        attr_is_null(attr_from<RefAttributeValue>(std::numeric_limits<double>::infinity())));
    EXPECT_FALSE(attr_is_null(attr_from<RefAttributeValue>("hello")));
    EXPECT_FALSE(attr_is_null(attr_from<RefAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff))));

    EXPECT_FALSE(attr_is_null(attr_from<RefAttributeValue>(false)));
    EXPECT_FALSE(attr_is_null(attr_from<RefAttributeValue>("")));
    EXPECT_FALSE(attr_is_null(attr_from<RefAttributeValue>(0.0)));
    EXPECT_FALSE(
        attr_is_null(attr_from<RefAttributeValue>(std::numeric_limits<double>::quiet_NaN())));
    EXPECT_FALSE(attr_is_null(attr_from<RefAttributeValue>(0)));
}

TEST(AttributeValueAs, as_bool)
{
    EXPECT_FALSE(attr_as_bool(attr_null<RefAttributeValue>()));
    EXPECT_TRUE(attr_as_bool(attr_from<RefAttributeValue>(true)));
    EXPECT_TRUE(attr_as_bool(attr_from<RefAttributeValue>(1.0)));
    EXPECT_TRUE(
        attr_as_bool(attr_from<RefAttributeValue>(std::numeric_limits<double>::infinity())));
    EXPECT_TRUE(attr_as_bool(attr_from<RefAttributeValue>("hello")));
    EXPECT_TRUE(attr_as_bool(attr_from<RefAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff))));

    EXPECT_FALSE(attr_as_bool(attr_from<RefAttributeValue>(false)));
    EXPECT_FALSE(attr_as_bool(attr_from<RefAttributeValue>("")));
    EXPECT_FALSE(attr_as_bool(attr_from<RefAttributeValue>(0.0)));
    EXPECT_FALSE(
        attr_as_bool(attr_from<RefAttributeValue>(std::numeric_limits<double>::quiet_NaN())));
    EXPECT_FALSE(attr_as_bool(attr_from<RefAttributeValue>(0)));
}

TEST(AttributeValueAs, as_number)
{
    EXPECT_EQ(attr_as_number(attr_null<RefAttributeValue>()), 0.0);
    EXPECT_EQ(attr_as_number(attr_from<RefAttributeValue>(42.0)), 42.0);
    EXPECT_EQ(attr_as_number(attr_from<RefAttributeValue>(true)), 1.0);
    EXPECT_EQ(attr_as_number(attr_from<RefAttributeValue>(false)), 0.0);
    EXPECT_EQ(
        attr_as_number(attr_from<RefAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff))),
        18446744073709551616.0);
    EXPECT_TRUE(std::isnan(attr_as_number(attr_from<RefAttributeValue>("42"))));
}

TEST(AttributeValueAs, as_uint64)
{
    EXPECT_EQ(attr_as_uint64(attr_null<RefAttributeValue>()), UINT64_C(0));
    EXPECT_EQ(attr_as_uint64(attr_from<RefAttributeValue>(42.0)), UINT64_C(42));
    EXPECT_EQ(attr_as_uint64(attr_from<RefAttributeValue>(true)), UINT64_C(1));
    EXPECT_EQ(attr_as_uint64(attr_from<RefAttributeValue>(false)), UINT64_C(0));
    EXPECT_EQ(
        attr_as_uint64(attr_from<RefAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff))),
        UINT64_C(0xffff'ffff'ffff'ffff));
    EXPECT_EQ(attr_as_uint64(attr_from<RefAttributeValue>("42")), UINT64_C(0));
}

TEST(AttributeValueAs, as_int64)
{
    EXPECT_EQ(attr_as_int64(attr_null<RefAttributeValue>()), INT64_C(0));
    EXPECT_EQ(attr_as_int64(attr_from<RefAttributeValue>(42.0)), INT64_C(42));
    EXPECT_EQ(attr_as_int64(attr_from<RefAttributeValue>(true)), INT64_C(1));
    EXPECT_EQ(attr_as_int64(attr_from<RefAttributeValue>(false)), INT64_C(0));
    EXPECT_EQ(
        attr_as_int64(attr_from<RefAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff))), INT64_C(-1));
    EXPECT_EQ(
        attr_as_int64(attr_from<RefAttributeValue>(UINT64_C(-8'000'000'000'000'000'000))),
        INT64_C(-8'000'000'000'000'000'000));
    EXPECT_EQ(attr_as_int64(attr_from<RefAttributeValue>("42")), INT64_C(0));
}

TEST(AttributeValueAs, as_string)
{
    EXPECT_EQ(attr_as_string(attr_null<RefAttributeValue>()), "");
    EXPECT_EQ(attr_as_string(attr_from<RefAttributeValue>("Hello, World!")), "Hello, World!");
    EXPECT_EQ(attr_as_string(attr_from<RefAttributeValue>("")), "");
    EXPECT_EQ(attr_as_string(attr_from<RefAttributeValue>(42.0)), "");
    EXPECT_EQ(attr_as_string(attr_from<RefAttributeValue>(true)), "");
    EXPECT_EQ(attr_as_string(attr_from<RefAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff))), "");
}

TEST(AttributeValueAs, as_color)
{
    EXPECT_EQ(attr_as_color(attr_null<RefAttributeValue>()), lm::ubvec4(0, 0, 0, 0));
    EXPECT_EQ(attr_as_color(attr_from<RefAttributeValue>(0xff0000ff)), lm::ubvec4(255, 0, 0, 255));
    EXPECT_EQ(attr_as_color(attr_from<RefAttributeValue>(0xff000080)), lm::ubvec4(128, 0, 0, 255));
    EXPECT_EQ(
        attr_as_color(attr_from_color<RefAttributeValue>(lm::ubvec4(1, 2, 3, 4))),
        lm::ubvec4(1, 2, 3, 4));
    EXPECT_EQ(
        attr_as_color(attr_from_color<RefAttributeValue>(lm::vec4(0.5, 0, 0, 1.0))),
        lm::ubvec4(128, 0, 0, 255));
}

TEST(AttributeValueTransform, none)
{
    auto attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_NONE,
        attr_from<RefAttributeValue>(42.0));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_get_number(attr), 42.0);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_NONE,
        attr_from_color<RefAttributeValue>(lm::vec4(1, 0, 1, 1)));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 0xffff00ff);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_NONE,
        attr_from<RefAttributeValue>("Hello"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kString);
    EXPECT_EQ(attr_get_string(attr), "Hello");

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_NONE,
        attr_from<RefAttributeValue>(true));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kBoolean);
    EXPECT_EQ(attr_get_bool(attr), true);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_NONE, attr_null<RefAttributeValue>());
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNull);
}

TEST(AttributeValueTransform, to_color)
{
    auto attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_COLOR,
        attr_from<RefAttributeValue>(42.0));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_get_number(attr), 42.0);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_COLOR,
        attr_from_color<RefAttributeValue>(lm::vec4(1, 0, 1, 1)));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 0xffff00ff);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_COLOR,
        attr_from<RefAttributeValue>("Hello"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 0);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_COLOR,
        attr_from<RefAttributeValue>("#f00"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 0xff0000ff);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_COLOR,
        attr_from<RefAttributeValue>("#12345678"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 0x78563412);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_COLOR,
        attr_from<RefAttributeValue>("blUe"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 0xffff0000);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_COLOR,
        attr_null<RefAttributeValue>());
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 0);
}

TEST(AttributeValueTransform, to_int)
{
    auto attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_INT,
        attr_from<RefAttributeValue>(42.0));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_get_number(attr), 42.0);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_INT,
        attr_from<RefAttributeValue>("Hello"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 0);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_INT,
        attr_from<RefAttributeValue>("42"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 42);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_INT,
        attr_from<RefAttributeValue>("-42"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_int64(attr), -42);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_INT, attr_null<RefAttributeValue>());
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_int64(attr), 0);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_INT,
        attr_from<RefAttributeValue>(true));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_int64(attr), 1);
}

TEST(AttributeValueTransform, to_uint)
{
    auto attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_UINT,
        attr_from<RefAttributeValue>(42.0));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 42);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_UINT,
        attr_from<RefAttributeValue>("Hello"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 0);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_UINT,
        attr_from<RefAttributeValue>("42"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 42);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_UINT,
        attr_from<RefAttributeValue>("-42"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 0);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_UINT, attr_null<RefAttributeValue>());
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 0);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_UINT,
        attr_from<RefAttributeValue>(true));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_uint64(attr), 1);
}

TEST(AttributeValueTransform, to_number)
{
    auto attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_NUMBER,
        attr_from<RefAttributeValue>(42.0));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_get_number(attr), 42.0);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_NUMBER,
        attr_from<RefAttributeValue>("Hello"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_TRUE(std::isnan(attr_as_number(attr)));

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_NUMBER,
        attr_from<RefAttributeValue>("42"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_number(attr), 42);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_NUMBER,
        attr_from<RefAttributeValue>("-42"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_number(attr), -42);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_NUMBER,
        attr_null<RefAttributeValue>());
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_number(attr), 0);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_NUMBER,
        attr_from<RefAttributeValue>(true));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kNumber);
    EXPECT_EQ(attr_as_number(attr), 1);
}

TEST(AttributeValueTransform, to_string)
{
    auto attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_STRING,
        attr_from<RefAttributeValue>(42.0));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kString);
    EXPECT_EQ(attr_get_string(attr), "42");

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_STRING,
        attr_from<RefAttributeValue>("Hello"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kString);
    EXPECT_EQ(attr_get_string(attr), "Hello");

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_STRING,
        attr_from<RefAttributeValue>(true));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kString);
    EXPECT_EQ(attr_get_string(attr), "true");

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_STRING,
        attr_null<RefAttributeValue>());
    EXPECT_EQ(attr_type(attr), AttributeValueType::kString);
    EXPECT_EQ(attr_get_string(attr), "");

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_STRING,
        attr_from<RefAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff)));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kString);
    EXPECT_EQ(attr_get_string(attr), "18446744073709551615");
}

TEST(AttributeValueTransform, to_boolean)
{
    auto attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_BOOLEAN,
        attr_from<RefAttributeValue>(42.0));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kBoolean);
    EXPECT_EQ(attr_get_bool(attr), true);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_BOOLEAN,
        attr_from<RefAttributeValue>(0.0));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kBoolean);
    EXPECT_EQ(attr_get_bool(attr), false);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_BOOLEAN,
        attr_from<RefAttributeValue>("Hello"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kBoolean);
    EXPECT_EQ(attr_get_bool(attr), false);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_BOOLEAN,
        attr_from<RefAttributeValue>("TRUE"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kBoolean);
    EXPECT_EQ(attr_get_bool(attr), true);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_BOOLEAN,
        attr_from<RefAttributeValue>(" true"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kBoolean);
    EXPECT_EQ(attr_get_bool(attr), true);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_BOOLEAN,
        attr_from<RefAttributeValue>("1"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kBoolean);
    EXPECT_EQ(attr_get_bool(attr), true);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_BOOLEAN,
        attr_from<RefAttributeValue>(" -45.1"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kBoolean);
    EXPECT_EQ(attr_get_bool(attr), true);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_BOOLEAN,
        attr_from<RefAttributeValue>("0"));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kBoolean);
    EXPECT_EQ(attr_get_bool(attr), false);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_BOOLEAN,
        attr_null<RefAttributeValue>());
    EXPECT_EQ(attr_type(attr), AttributeValueType::kBoolean);
    EXPECT_EQ(attr_get_bool(attr), false);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_BOOLEAN,
        attr_from<RefAttributeValue>(true));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kBoolean);
    EXPECT_EQ(attr_get_bool(attr), true);

    attr = attr_transform<OwnedAttributeValue>(
        hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_BOOLEAN,
        attr_from<RefAttributeValue>(UINT64_C(0xffff'ffff'ffff'ffff)));
    EXPECT_EQ(attr_type(attr), AttributeValueType::kBoolean);
    EXPECT_EQ(attr_get_bool(attr), true);
}

TEST(AttributeValueHash, number)
{
    auto attr1 = attr_from<RefAttributeValue>(42.0);
    auto attr2 = attr_from<RefAttributeValue>(42.0);
    auto attr3 = attr_from<RefAttributeValue>((uint64_t)42);
    auto attr4 = attr_from<RefAttributeValue>(42.0000000000002);

    EXPECT_EQ(attr_hashed(attr1), attr_hashed(attr2));
    EXPECT_EQ(attr_hashed(attr1), attr_hashed(attr3));
    EXPECT_NE(attr_hashed(attr1), attr_hashed(attr4));

    // This is just to test that different platforms give the same hash.
    // This is important for random functions in style scripts.
    EXPECT_EQ(attr_hashed(attr1), UINT64_C(3704004423728601426));
}

TEST(AttributeValueHash, uint64)
{
    auto attr1 = attr_from<RefAttributeValue>(UINT64_C(0xffff'eeee'dddd'cccc));
    auto attr2 = attr_from<RefAttributeValue>(UINT64_C(0xffff'eeee'dddd'cccc));
    auto attr4 = attr_from<RefAttributeValue>(UINT64_C(0xffff'cccc'eeee'aaaa));

    EXPECT_EQ(attr_hashed(attr1), attr_hashed(attr2));
    EXPECT_NE(attr_hashed(attr1), attr_hashed(attr4));

    // This is just to test that different platforms give the same hash.
    // This is important for random functions in style scripts.
    EXPECT_EQ(attr_hashed(attr1), UINT64_C(6414780418214494523));
}

TEST(AttributeValueHash, int64)
{
    auto attr1 = attr_from<RefAttributeValue>(INT64_C(0x7fff'eeee'dddd'cccc));
    auto attr2 = attr_from<RefAttributeValue>(INT64_C(0x7fff'eeee'dddd'cccc));
    auto attr4 = attr_from<RefAttributeValue>(INT64_C(0x7fff'cccc'eeee'aaaa));

    EXPECT_EQ(attr_hashed(attr1), attr_hashed(attr2));
    EXPECT_NE(attr_hashed(attr1), attr_hashed(attr4));

    // This is just to test that different platforms give the same hash.
    // This is important for random functions in style scripts.
    EXPECT_EQ(attr_hashed(attr1), UINT64_C(11516578154770931225));
}

TEST(AttributeValueHash, string)
{
    auto attr1 = attr_from<RefAttributeValue>("Hello");
    auto attr2 = attr_from<RefAttributeValue>("Hello");
    auto attr4 = attr_from<RefAttributeValue>("HELLO");

    EXPECT_EQ(attr_hashed(attr1), attr_hashed(attr2));
    EXPECT_NE(attr_hashed(attr1), attr_hashed(attr4));

    // This is just to test that different platforms give the same hash.
    // This is important for random functions in style scripts.
    EXPECT_EQ(attr_hashed(attr1), UINT64_C(11578065966165735990));
}
