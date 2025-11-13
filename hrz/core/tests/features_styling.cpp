#include "hrz/common/attributes.h"
#include "hrz/common/blob_allocator.h"
#include "hrz/common/style.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/style/script.h"
#include "hrz/fnd/defines.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/log.h"

#include <gtest/gtest.h>

#include <bit>
#include <limits>

namespace
{
class TestJobContext : public hrz_jobs::JobContext
{
public:
    TestJobContext(hrz::BlobAllocator* blob_allocator) : blob_allocator(blob_allocator) {}

    int get_worker_id() const override { return 0; }

    hrz::BlobAllocator* get_blob_allocator() const override { return blob_allocator; }

    hrz::FontRasterizer* get_font_rasterizer() const override { return nullptr; }

    hrz::monitoring::ResourceOwner get_resource_owner() const override { return {}; }

private:
    hrz::BlobAllocator* blob_allocator;
};
} // namespace

namespace
{
using namespace hrz::style;
using namespace hrz::vector_data;

class FeatureStyling : public ::testing::Test
{
protected:
    hrz::BlobAllocator* blob_allocator;
    TestJobContext job_context;

    FeatureStyling() :
        blob_allocator(hrz::blobs::create_allocator(1024 * 1024, true)), job_context(blob_allocator)
    {
    }

    void add_repr(FeaturesStylingData& data, uint32_t id, const char* name)
    {
        data.representations.push_back({id, name});
    }

    AttributeValuesBuilder new_attribute()
    {
        assert(blob_allocator);
        return AttributeValuesBuilder(16, blob_allocator, job_context.get_resource_owner());
    }
};

TEST_F(FeatureStyling, empty)
{
    static const char* script = "";
    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});

    hrz::style::Ast full_ast;
    ASSERT_TRUE(parser->parse(*lexer, full_ast));
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "zero");
    data.feature_count = 3;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(0, resp.features.instances.size());
}

TEST_F(FeatureStyling, one_emit)
{
    static const char* script = "emit \"repr\";";
    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});

    uint32_t repr_id = 0;
    hrz::style::Ast full_ast;
    ASSERT_TRUE(parser->parse(*lexer, full_ast));
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, repr_id, "repr");
    add_repr(data, 123, "zero");
    data.feature_count = 3;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(3, resp.features.instances.size());
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(0).feature_index);
    EXPECT_EQ(1, resp.features.instances.get_data().unsafe_at(1).feature_index);
    EXPECT_EQ(2, resp.features.instances.get_data().unsafe_at(2).feature_index);
    EXPECT_EQ(repr_id, resp.features.instances.get_data().unsafe_at(0).repr_id);
    EXPECT_EQ(repr_id, resp.features.instances.get_data().unsafe_at(1).repr_id);
    EXPECT_EQ(repr_id, resp.features.instances.get_data().unsafe_at(2).repr_id);
}

TEST_F(FeatureStyling, stop_at_first_emit)
{
    static const char* script = "emit \"repr\"; emit \"zero\";";
    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});

    uint32_t repr_id = 0;
    hrz::style::Ast full_ast;
    ASSERT_TRUE(parser->parse(*lexer, full_ast));
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, repr_id, "repr");
    add_repr(data, 123, "zero");
    data.feature_count = 3;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(3, resp.features.instances.size());
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(0).feature_index);
    EXPECT_EQ(1, resp.features.instances.get_data().unsafe_at(1).feature_index);
    EXPECT_EQ(2, resp.features.instances.get_data().unsafe_at(2).feature_index);
    EXPECT_EQ(repr_id, resp.features.instances.get_data().unsafe_at(0).repr_id);
    EXPECT_EQ(repr_id, resp.features.instances.get_data().unsafe_at(1).repr_id);
    EXPECT_EQ(repr_id, resp.features.instances.get_data().unsafe_at(2).repr_id);
}

TEST_F(FeatureStyling, discard)
{
    static const char* script = "discard; emit \"repr\";";
    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});

    hrz::style::Ast full_ast;
    ASSERT_TRUE(parser->parse(*lexer, full_ast));
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 789, "repr");
    add_repr(data, 123, "zero");
    data.feature_count = 3;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(0, resp.features.instances.size());
}

TEST_F(FeatureStyling, fork_simple)
{
    static const char* script =
        "fork { fork { emit \"one\"; } emit \"two\"; } fork { emit \"three\"; } "
        "emit \"four\"; ";
    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t one_id = 1;
    uint32_t two_id = 2;
    uint32_t three_id = 3;
    uint32_t four_id = 4;

    hrz::style::Ast full_ast;
    ASSERT_TRUE(parser->parse(*lexer, full_ast));
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, one_id, "one");
    add_repr(data, two_id, "two");
    add_repr(data, three_id, "three");
    add_repr(data, four_id, "four");
    data.feature_count = 3;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(12, resp.features.instances.size());
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(0).feature_index);
    EXPECT_EQ(1, resp.features.instances.get_data().unsafe_at(1).feature_index);
    EXPECT_EQ(2, resp.features.instances.get_data().unsafe_at(2).feature_index);
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(3).feature_index);
    EXPECT_EQ(1, resp.features.instances.get_data().unsafe_at(4).feature_index);
    EXPECT_EQ(2, resp.features.instances.get_data().unsafe_at(5).feature_index);
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(6).feature_index);
    EXPECT_EQ(1, resp.features.instances.get_data().unsafe_at(7).feature_index);
    EXPECT_EQ(2, resp.features.instances.get_data().unsafe_at(8).feature_index);
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(9).feature_index);
    EXPECT_EQ(1, resp.features.instances.get_data().unsafe_at(10).feature_index);
    EXPECT_EQ(2, resp.features.instances.get_data().unsafe_at(11).feature_index);
    EXPECT_EQ(one_id, resp.features.instances.get_data().unsafe_at(0).repr_id);
    EXPECT_EQ(one_id, resp.features.instances.get_data().unsafe_at(1).repr_id);
    EXPECT_EQ(one_id, resp.features.instances.get_data().unsafe_at(2).repr_id);
    EXPECT_EQ(two_id, resp.features.instances.get_data().unsafe_at(3).repr_id);
    EXPECT_EQ(two_id, resp.features.instances.get_data().unsafe_at(4).repr_id);
    EXPECT_EQ(two_id, resp.features.instances.get_data().unsafe_at(5).repr_id);
    EXPECT_EQ(three_id, resp.features.instances.get_data().unsafe_at(6).repr_id);
    EXPECT_EQ(three_id, resp.features.instances.get_data().unsafe_at(7).repr_id);
    EXPECT_EQ(three_id, resp.features.instances.get_data().unsafe_at(8).repr_id);
    EXPECT_EQ(four_id, resp.features.instances.get_data().unsafe_at(9).repr_id);
    EXPECT_EQ(four_id, resp.features.instances.get_data().unsafe_at(10).repr_id);
    EXPECT_EQ(four_id, resp.features.instances.get_data().unsafe_at(11).repr_id);
}

TEST_F(FeatureStyling, fork_exit_early)
{
    static const char* script = "fork { emit \"one\"; emit \"two\"; } fork{} emit \"three\";";
    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t one_id = 1;
    uint32_t three_id = 3;

    hrz::style::Ast full_ast;
    ASSERT_TRUE(parser->parse(*lexer, full_ast));
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, one_id, "one");
    add_repr(data, 2, "two");
    add_repr(data, three_id, "three");
    data.feature_count = 3;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(6, resp.features.instances.size());
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(0).feature_index);
    EXPECT_EQ(1, resp.features.instances.get_data().unsafe_at(1).feature_index);
    EXPECT_EQ(2, resp.features.instances.get_data().unsafe_at(2).feature_index);
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(3).feature_index);
    EXPECT_EQ(1, resp.features.instances.get_data().unsafe_at(4).feature_index);
    EXPECT_EQ(2, resp.features.instances.get_data().unsafe_at(5).feature_index);
    EXPECT_EQ(one_id, resp.features.instances.get_data().unsafe_at(0).repr_id);
    EXPECT_EQ(one_id, resp.features.instances.get_data().unsafe_at(1).repr_id);
    EXPECT_EQ(one_id, resp.features.instances.get_data().unsafe_at(2).repr_id);
    EXPECT_EQ(three_id, resp.features.instances.get_data().unsafe_at(3).repr_id);
    EXPECT_EQ(three_id, resp.features.instances.get_data().unsafe_at(4).repr_id);
    EXPECT_EQ(three_id, resp.features.instances.get_data().unsafe_at(5).repr_id);
}

TEST_F(FeatureStyling, set_literal)
{
    static const char* script =
        "set \"i\" = 75; set \"u\" = 12; set \"i\" = -78; set \"s\" = \"world\"; set \"d\" = -1.5; "
        "set \"s\" = \"hello\"; emit \"one\";";
    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t one_id = 1;

    uint16_t int_id = 0;
    parser->add_property(int_id, "i");

    uint16_t uint_id = 1;
    parser->add_property(uint_id, "u");

    uint16_t double_id = 2;
    parser->add_property(double_id, "d");

    uint16_t string_id = 3;
    parser->add_property(string_id, "s");

    hrz::style::Ast full_ast;
    ASSERT_TRUE(parser->parse(*lexer, full_ast));
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, one_id, "one");
    data.feature_count = 2;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(2, resp.features.instances.size());
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(0).feature_index);
    EXPECT_EQ(1, resp.features.instances.get_data().unsafe_at(1).feature_index);
    EXPECT_EQ(one_id, resp.features.instances.get_data().unsafe_at(0).repr_id);
    EXPECT_EQ(one_id, resp.features.instances.get_data().unsafe_at(1).repr_id);
    ASSERT_EQ(resp.features.prps.size(), resp.features.values.size());
    ASSERT_EQ(0, resp.features.instances.get_data().unsafe_at(0).first_prp);
    ASSERT_EQ(4, resp.features.instances.get_data().unsafe_at(0).prp_count);
    ASSERT_EQ(4, resp.features.instances.get_data().unsafe_at(1).first_prp);
    ASSERT_EQ(4, resp.features.instances.get_data().unsafe_at(1).prp_count);

    auto reader = resp.features.get_values_reader();

    for (const auto& inst : resp.features.instances.get_data())
    {
        bool seen[4] = {false, false, false, false};
        RefAttributeValue values[4];
        values[int_id] = attr_from<RefAttributeValue>(-78);
        values[uint_id] = attr_from<RefAttributeValue>(12);
        values[double_id] = attr_from<RefAttributeValue>(-1.5);
        values[string_id] = attr_from<RefAttributeValue>("hello");

        for (int i = 0; i < 4; ++i)
        {
            uint32_t prp_id = resp.features.prps.get_data().unsafe_at(inst.first_prp + i);
            EXPECT_EQ(reader.as_ref(inst.first_prp + i), values[prp_id]);
            seen[prp_id] = true;
        }

        EXPECT_TRUE(seen[0]);
        EXPECT_TRUE(seen[1]);
        EXPECT_TRUE(seen[2]);
        EXPECT_TRUE(seen[3]);
    }
}

TEST_F(FeatureStyling, set_attribute)
{
    static const char* script =
        "set \"i\" = attr(\"i\"); set \"u\" = attr(\"u\"); set \"s\" = attr(\"s\"); set \"d\" = "
        "attr(\"d\"); emit \"one\";";
    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t one_id = 1;

    uint16_t int_id = 0;
    parser->add_property(int_id, "i");

    uint16_t uint_id = 1;
    parser->add_property(uint_id, "u");

    uint16_t double_id = 2;
    parser->add_property(double_id, "d");

    uint16_t string_id = 3;
    parser->add_property(string_id, "s");

    /*int int_attr =*/parser->add_attribute("i");
    /*int uint_attr =*/parser->add_attribute("u");
    /*int double_attr =*/parser->add_attribute("d");
    /*int string_attr =*/parser->add_attribute("s");

    hrz::style::Ast full_ast;
    ASSERT_TRUE(parser->parse(*lexer, full_ast));
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, one_id, "one");
    data.feature_count = 2;

    {
        auto attr = new_attribute();
        attr.push((int64_t)-1);
        attr.push((int64_t)-2);
        data.attributes.insert({int_id, attr.finalize(0).value()});
    }

    {
        auto attr = new_attribute();
        attr.push((uint64_t)1);
        attr.push((uint64_t)2);
        data.attributes.insert({uint_id, attr.finalize(0).value()});
    }

    {
        auto attr = new_attribute();
        attr.push(1.0);
        attr.push(2.0);
        data.attributes.insert({double_id, attr.finalize(0).value()});
    }

    {
        auto attr = new_attribute();
        attr.push((std::string_view) "one");
        attr.push((std::string_view) "two");
        data.attributes.insert({string_id, attr.finalize(0).value()});
    }

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(2, resp.features.instances.size());
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(0).feature_index);
    EXPECT_EQ(1, resp.features.instances.get_data().unsafe_at(1).feature_index);
    EXPECT_EQ(one_id, resp.features.instances.get_data().unsafe_at(0).repr_id);
    EXPECT_EQ(one_id, resp.features.instances.get_data().unsafe_at(1).repr_id);
    ASSERT_EQ(resp.features.prps.size(), resp.features.values.size());
    ASSERT_EQ(0, resp.features.instances.get_data().unsafe_at(0).first_prp);
    ASSERT_EQ(4, resp.features.instances.get_data().unsafe_at(0).prp_count);
    ASSERT_EQ(4, resp.features.instances.get_data().unsafe_at(1).first_prp);
    ASSERT_EQ(4, resp.features.instances.get_data().unsafe_at(1).prp_count);

    auto reader = resp.features.get_values_reader();

    for (const auto& inst : resp.features.instances.get_data())
    {
        bool seen[4] = {false, false, false, false};
        RefAttributeValue values[4];

        if (inst.feature_index == 0)
        {
            values[int_id] = attr_from<RefAttributeValue>(-1);
            values[uint_id] = attr_from<RefAttributeValue>(1);
            values[double_id] = attr_from<RefAttributeValue>(1.0);
            values[string_id] = attr_from<RefAttributeValue>("one");
        }
        else if (inst.feature_index == 1)
        {
            values[int_id] = attr_from<RefAttributeValue>(-2);
            values[uint_id] = attr_from<RefAttributeValue>(2);
            values[double_id] = attr_from<RefAttributeValue>(2.0);
            values[string_id] = attr_from<RefAttributeValue>("two");
        }

        for (int i = 0; i < 4; ++i)
        {
            uint32_t prp_id = resp.features.prps.get_data().unsafe_at(inst.first_prp + i);
            EXPECT_EQ(reader.as_ref(inst.first_prp + i), values[prp_id]);
            seen[prp_id] = true;
        }

        EXPECT_TRUE(seen[0]);
        EXPECT_TRUE(seen[1]);
        EXPECT_TRUE(seen[2]);
        EXPECT_TRUE(seen[3]);
    }
}

TEST_F(FeatureStyling, set_uniform)
{
    static const char* script = "set \"u\" = uniform(\"tile_z\"); emit 1;";
    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});

    uint16_t prp_id = 1;
    parser->add_property(prp_id, "u");

    uint32_t uniform_id = (uint32_t)parser->add_uniform("tile_z");

    hrz::style::Ast full_ast;
    ASSERT_TRUE(parser->parse(*lexer, full_ast));
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 1, "one");
    data.feature_count = 1;

    data.uniforms.insert({uniform_id, attr_from<OwnedAttributeValue>(8)});

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(1, resp.features.instances.size());
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(0).feature_index);
    EXPECT_EQ(1, resp.features.instances.get_data().unsafe_at(0).repr_id);
    ASSERT_EQ(resp.features.prps.size(), resp.features.values.size());
    ASSERT_EQ(0, resp.features.instances.get_data().unsafe_at(0).first_prp);
    ASSERT_EQ(1, resp.features.instances.get_data().unsafe_at(0).prp_count);
    ASSERT_EQ(8, resp.features.get_values_reader().as_uint64(0));
}

TEST_F(FeatureStyling, decimal_number_interpretation)
{
    static const char* script =
        "set \"x\" = div(1, 2);\n"
        "set \"y\" = div(1, 2.0);\n"
        "set \"z\" = div(1.0, 2);\n"
        "emit 0;";
    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});

    uint16_t prp_x = 1;
    uint16_t prp_y = 2;
    uint16_t prp_z = 3;
    parser->add_property(prp_x, "x");
    parser->add_property(prp_y, "y");
    parser->add_property(prp_z, "z");

    hrz::style::Ast full_ast;
    ASSERT_TRUE(parser->parse(*lexer, full_ast));
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    data.feature_count = 1;
    add_repr(data, 0, "r");

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(1, resp.features.instances.size());

    const auto& inst = resp.features.instances.get_data().unsafe_at(0);
    ASSERT_EQ(0, inst.first_prp);
    ASSERT_EQ(3, inst.prp_count);

    for (unsigned int i = 0; i < inst.prp_count; ++i)
    {
        unsigned int prp_offset = inst.first_prp + i;
        int prp_id = resp.features.prps.get_data().unsafe_at(prp_offset);
        if (prp_id == prp_x)
        {
            ASSERT_EQ(0.5, resp.features.get_values_reader().as_number(prp_offset));
        }
        else if (prp_id == prp_y)
        {
            ASSERT_EQ(0.5, resp.features.get_values_reader().as_number(prp_offset));
        }
        else if (prp_id == prp_z)
        {
            ASSERT_EQ(0.5, resp.features.get_values_reader().as_number(prp_offset));
        }
    }
}

TEST_F(FeatureStyling, set_attribute_numeric_cast)
{
    static const char* script =
        "fork { set \"d\" = attr(\"d\"); emit \"one\"; }"
        "fork { set \"d\" = attr(\"i\"); emit \"two\"; }"
        "fork { set \"d\" = attr(\"u\"); emit \"three\"; }";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t one_id = 1;
    uint32_t two_id = 2;
    uint32_t three_id = 3;

    uint16_t double_id = 0;
    parser->add_property(double_id, "d");

    uint32_t int_attr = parser->add_attribute("i");
    uint32_t uint_attr = parser->add_attribute("u");
    uint32_t double_attr = parser->add_attribute("d");

    hrz::style::Ast full_ast;
    ASSERT_TRUE(parser->parse(*lexer, full_ast));
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, one_id, "one");
    add_repr(data, two_id, "two");
    add_repr(data, three_id, "three");
    data.feature_count = 1;

    {
        auto attr = new_attribute();
        attr.push((int64_t)-1);
        data.attributes.insert({int_attr, attr.finalize(0).value()});
    }

    {
        auto attr = new_attribute();
        attr.push((int64_t)123456789);
        data.attributes.insert({uint_attr, attr.finalize(0).value()});
    }

    {
        auto attr = new_attribute();
        attr.push(1.0);
        data.attributes.insert({double_attr, attr.finalize(0).value()});
    }

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(3, resp.features.instances.size());
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(0).feature_index);
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(1).feature_index);
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(2).feature_index);
    EXPECT_EQ(one_id, resp.features.instances.get_data().unsafe_at(0).repr_id);
    EXPECT_EQ(two_id, resp.features.instances.get_data().unsafe_at(1).repr_id);
    EXPECT_EQ(three_id, resp.features.instances.get_data().unsafe_at(2).repr_id);
    ASSERT_EQ(resp.features.prps.size(), resp.features.values.size());
    ASSERT_EQ(0, resp.features.instances.get_data().unsafe_at(0).first_prp);
    ASSERT_EQ(1, resp.features.instances.get_data().unsafe_at(0).prp_count);
    ASSERT_EQ(1, resp.features.instances.get_data().unsafe_at(1).first_prp);
    ASSERT_EQ(1, resp.features.instances.get_data().unsafe_at(1).prp_count);
    ASSERT_EQ(2, resp.features.instances.get_data().unsafe_at(2).first_prp);
    ASSERT_EQ(1, resp.features.instances.get_data().unsafe_at(2).prp_count);
    ASSERT_EQ(double_id, resp.features.prps.get_data().unsafe_at(0));
    ASSERT_EQ(double_id, resp.features.prps.get_data().unsafe_at(1));
    ASSERT_EQ(double_id, resp.features.prps.get_data().unsafe_at(2));

    ASSERT_EQ(1.0, resp.features.get_values_reader().as_number(0));
    ASSERT_EQ(-1.0, resp.features.get_values_reader().as_number(1));
    ASSERT_EQ(123456789.0, resp.features.get_values_reader().as_number(2));
}

TEST_F(FeatureStyling, set_with_fork)
{
    static const char* script =
        "set \"a\" = 1;"
        "fork {"
        "    emit \"r\";"
        "}"
        "fork {"
        "    set \"a\" = 2;"
        "    emit \"r\";"
        "}"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t repr = 0;
    uint16_t prp = 0;
    parser->add_property(prp, "a");

    hrz::style::Ast full_ast;
    ASSERT_TRUE(parser->parse(*lexer, full_ast));
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, repr, "r");
    data.feature_count = 1;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(3, resp.features.instances.size());
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(0).feature_index);
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(1).feature_index);
    EXPECT_EQ(0, resp.features.instances.get_data().unsafe_at(2).feature_index);

    EXPECT_EQ(repr, resp.features.instances.get_data().unsafe_at(0).repr_id);
    EXPECT_EQ(repr, resp.features.instances.get_data().unsafe_at(1).repr_id);
    EXPECT_EQ(repr, resp.features.instances.get_data().unsafe_at(2).repr_id);

    ASSERT_EQ(resp.features.prps.size(), resp.features.values.size());
    ASSERT_EQ(0, resp.features.instances.get_data().unsafe_at(0).first_prp);
    ASSERT_EQ(1, resp.features.instances.get_data().unsafe_at(0).prp_count);
    ASSERT_EQ(1, resp.features.instances.get_data().unsafe_at(1).first_prp);
    ASSERT_EQ(1, resp.features.instances.get_data().unsafe_at(1).prp_count);
    ASSERT_EQ(2, resp.features.instances.get_data().unsafe_at(2).first_prp);
    ASSERT_EQ(1, resp.features.instances.get_data().unsafe_at(2).prp_count);

    uint64_t values[3] = {1, 2, 1};

    for (size_t i = 0; i < 3; ++i)
    {
        const auto& inst = resp.features.instances.get_data().unsafe_at(i);
        EXPECT_EQ(prp, resp.features.prps.get_data().unsafe_at(inst.first_prp + 0));
        EXPECT_EQ(values[i], resp.features.get_values_reader().as_uint64(inst.first_prp + 0));
    }
}

TEST_F(FeatureStyling, set_colorize_numeric)
{
    static const char* script =
        "set \"color\" = colorize(\"numeric_palette\", attr(\"height\"));"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});

    uint16_t prp_id = 0;
    parser->add_property(prp_id, "color");
    uint32_t attr_id = parser->add_attribute("height");
    parser->add_palette("numeric_palette");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    hrz_proto::Palette palette;
    palette.set_type(hrz_proto::PaletteType::NUMERIC);
    palette.mutable_numeric()->set_interpolation_mode(hrz_proto::ColorInterpolationMode::OKLAB);
    palette.set_name("numeric_palette");

    hrz_proto::Color lower;
    lower.set_r(1);
    lower.set_g(1);
    lower.set_b(1);
    lower.set_a(1);

    auto color_point = palette.mutable_numeric()->add_color_points();
    color_point->set_value(0.0f);
    color_point->mutable_first_color()->CopyFrom(lower);
    color_point->mutable_second_color()->CopyFrom(lower);

    hrz_proto::Color upper;
    upper.set_r(0);
    upper.set_g(0);
    upper.set_b(0);
    upper.set_a(1);

    color_point = palette.mutable_numeric()->add_color_points();
    color_point->set_value(100.0f);
    color_point->mutable_first_color()->CopyFrom(upper);
    color_point->mutable_second_color()->CopyFrom(upper);

    hrz_proto::Color nan_color;
    nan_color.set_r(1);
    nan_color.set_g(0);
    nan_color.set_b(0);
    nan_color.set_a(1);

    palette.mutable_numeric()->mutable_nan_color()->CopyFrom(nan_color);

    data.palettes.push_back(hrz::palette::from_proto(palette));

    auto attr = new_attribute();
    for (int i = 0; i < 5; ++i)
    {
        double height = i * 20;
        attr.push(height);
    }
    data.attributes.insert({attr_id, attr.finalize(0).value()});

    data.feature_count = 5;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(5, resp.features.instances.size());

    uint32_t values[5] = {0xffffffff, 0xffbebebe, 0xff808080, 0xff484848, 0xff161616};

    for (size_t i = 0; i < 5; ++i)
    {
        const auto& inst = resp.features.instances.get_data().unsafe_at(i);
        ASSERT_EQ(1, inst.prp_count);
        size_t index = inst.feature_index;
        EXPECT_EQ(prp_id, resp.features.prps.get_data().unsafe_at(inst.first_prp + 0));
        EXPECT_EQ(values[index], resp.features.get_values_reader().as_uint64(inst.first_prp + 0));
    }
}

TEST_F(FeatureStyling, set_colorize_labels)
{
    static const char* script =
        "set \"color\" = colorize(\"label_palette\", attr(\"name\"));"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});

    uint16_t prp_id = 0;
    parser->add_property(prp_id, "color");
    uint32_t attr_id = parser->add_attribute("name");
    parser->add_palette("label_palette");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    hrz_proto::Palette palette;
    palette.set_type(hrz_proto::PaletteType::LABEL);
    palette.set_name("label_palette");

    hrz_proto::Color a_color;
    a_color.set_r(1);
    a_color.set_g(1);
    a_color.set_b(1);
    a_color.set_a(1);

    auto label = palette.mutable_label()->add_labels();
    label->set_label("A");
    label->mutable_color()->CopyFrom(a_color);

    hrz_proto::Color b_color;
    b_color.set_r(0.5f);
    b_color.set_g(0.5f);
    b_color.set_b(0.5f);
    b_color.set_a(1);

    label = palette.mutable_label()->add_labels();
    label->set_label("B");
    label->mutable_color()->CopyFrom(b_color);

    hrz_proto::Color default_color;
    default_color.set_r(1);
    default_color.set_g(0);
    default_color.set_b(1);
    default_color.set_a(1);

    palette.mutable_label()->mutable_default_color()->CopyFrom(default_color);

    data.palettes.push_back(hrz::palette::from_proto(palette));

    auto attr = new_attribute();
    attr.push((std::string_view) "A");
    attr.push((std::string_view) "C");
    attr.push((std::string_view) "B");
    data.attributes.insert({attr_id, attr.finalize(0).value()});

    data.feature_count = 3;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(3, resp.features.instances.size());

    uint32_t values[3] = {0xffffffff, 0xffff00ff, 0xff808080};

    for (size_t i = 0; i < 3; ++i)
    {
        const auto& inst = resp.features.instances.get_data().unsafe_at(i);
        ASSERT_EQ(1, inst.prp_count);
        size_t index = inst.feature_index;
        EXPECT_EQ(prp_id, resp.features.prps.get_data().unsafe_at(inst.first_prp + 0));
        EXPECT_EQ(values[index], resp.features.get_values_reader().as_uint64(inst.first_prp + 0));
    }
}

TEST_F(FeatureStyling, double_precision)
{
    static const char* script =
        // The number isn't representable precisely by a double but can be with a uint64. The parser
        // should detect that and allow the number to be either used as a double or a uint64.
        "set \"u\" = 9007199254740993.0;"
        // The number isn't representable precisely by an int64 but can be with a double. The parser
        // should detect that and only use the double type.
        "set \"d\" = -9007199254740992;"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "u");
    parser->add_property(1, "d");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    data.feature_count = 1;

    add_repr(data, 0, "r");

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(1, resp.features.instances.size());

    const auto& inst = resp.features.instances.get_data().unsafe_at(0);
    ASSERT_EQ(2, inst.prp_count);

    for (unsigned int i = 0; i < inst.prp_count; ++i)
    {
        int prp_id = resp.features.prps.get_data().unsafe_at(inst.first_prp + i);
        if (prp_id == 0)
        {
            EXPECT_EQ(
                9007199254740993, resp.features.get_values_reader().as_number(inst.first_prp + i));
        }
        else if (prp_id == 1)
        {
            EXPECT_EQ(
                -9007199254740992, resp.features.get_values_reader().as_number(inst.first_prp + i));
        }
    }
}

TEST_F(FeatureStyling, fmt)
{
    static const char* scripts[] = {
        "set \"txt\" = fmt(\"{}\", attr(\"i\")); emit \"r\";",
        "set \"txt\" = fmt(\"{}\", attr(\"u\")); emit \"r\";",
        "set \"txt\" = fmt(\"{}\", attr(\"d\")); emit \"r\";",
        "set \"txt\" = fmt(\"{}\", attr(\"s\")); emit \"r\";",
        "set \"txt\" = fmt(\"{}\", mul(inv(attr(\"u\")),2)); emit \"r\";",
        "set \"txt\" = fmt(\"{}{}{}\", attr(\"i\"), attr(\"u\"), attr(\"d\")); "
        "emit \"r\";",
        "set \"txt\" = fmt(\"{}{}\", mul(attr(\"i\"), 2), inv(add(2, attr(\"u\")))); emit \"r\";",
        "set \"txt\" = fmt(\"{}\", fmt(\"{}\", attr(\"u\"))); emit \"r\";",
    };

    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);

    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "txt");
    /*uint32_t attr_i =*/parser->add_attribute("i");
    /*uint32_t attr_u =*/parser->add_attribute("u");
    /*uint32_t attr_d =*/parser->add_attribute("d");
    /*uint32_t attr_s =*/parser->add_attribute("s");

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);

        hrz::style::Ast full_ast;
        auto lexer = Lexer::create(scripts[i]);
        auto a = parser->parse(*lexer, full_ast);
        auto ast = std::make_shared<hrz::style::FlatAst>();
        ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));
        ASSERT_EQ(Result::Ok, a.type);
        ASSERT_TRUE(a);

        FeaturesStylingData data;
        data.ast = ast;
        add_repr(data, 0, "r");

        {
            auto attr = new_attribute();
            attr.push((int64_t)-1);
            data.attributes.insert({0, attr.finalize(0).value()});
        }
        {
            auto attr = new_attribute();
            attr.push((uint64_t)1);
            data.attributes.insert({1, attr.finalize(0).value()});
        }
        {
            auto attr = new_attribute();
            attr.push(1.0);
            data.attributes.insert({2, attr.finalize(0).value()});
        }
        {
            auto attr = new_attribute();
            attr.push((std::string_view) "1");
            data.attributes.insert({3, attr.finalize(0).value()});
        }

        data.feature_count = 1;

        StylingResult resp;
        auto res = hrz_jobs::style_features::run(data, resp, job_context);

        ASSERT_EQ(hrz::JobResult::SUCCESS, res);
        ASSERT_EQ(1, resp.features.instances.size());
    }
}

TEST_F(FeatureStyling, fmt_value)
{
    static const char* scripts[] = {
        "set \"txt\" = fmt(\"{}\", attr(\"i\")); emit \"r\";",
    };

    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);

    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_attribute("i");
    parser->add_property(0, "txt");

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);

        hrz::style::Ast full_ast;
        auto lexer = Lexer::create(scripts[i]);
        auto a = parser->parse(*lexer, full_ast);
        auto ast = std::make_shared<hrz::style::FlatAst>();
        ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));
        ASSERT_EQ(Result::Ok, a.type);
        ASSERT_TRUE(a);

        FeaturesStylingData data;
        data.ast = ast;
        add_repr(data, 0, "r");

        {
            auto attr = new_attribute();
            attr.push((int64_t)100);
            data.attributes.insert({0, attr.finalize(0).value()});
        }

        data.feature_count = 1;

        StylingResult resp;
        auto res = hrz_jobs::style_features::run(data, resp, job_context);

        ASSERT_EQ(hrz::JobResult::SUCCESS, res);
        ASSERT_EQ(1, resp.features.instances.size());
        ASSERT_EQ(1, resp.features.values.size());
        ASSERT_EQ("100", resp.features.get_values_reader().as_string(0));
    }
}

TEST_F(FeatureStyling, arithmetic_expressions)
{
    static const char* scripts[] = {
        "set \"i\" = mul(2, 3); emit \"r\";",
        "set \"i\" = mul(attr(\"i\"), 2); emit \"r\";",
        "set \"i\" = div(4, 2); emit \"r\";",
        "set \"i\" = div(attr(\"i\"), 2); emit \"r\";",
        "set \"i\" = mod(5, 3); emit \"r\";",
        "set \"i\" = mod(-5, 3); emit \"r\";",
        "set \"i\" = mod(attr(\"i\"), 2); emit \"r\";",
        "set \"i\" = add(2, 3); emit \"r\";",
        "set \"i\" = add(attr(\"i\"), 2); emit \"r\";",
        "set \"i\" = sub(2, 3); emit \"r\";",
        "set \"i\" = sub(attr(\"i\"), 7); emit \"r\";",
        "set \"i\" = abs(2); emit \"r\";",
        "set \"i\" = abs(-2); emit \"r\";",
        "set \"i\" = abs(attr(\"i\")); emit \"r\";",
        "set \"i\" = neg(2); emit \"r\";",
        "set \"i\" = neg(attr(\"i\")); emit \"r\";",

        // More complex operations.
        "set \"i\" = abs(mul(add(attr(\"i\"), -5), 2)); emit \"r\";",
        "set \"i\" = sub(div(attr(\"i\"), 4), 5); emit \"r\";",
        "set \"i\" = sub(mod(attr(\"i\"), 4), 5); emit \"r\";",

        // Division by 0.
        "set \"i\" = inv(0); emit \"r\";",
        "set \"i\" = div(1, 0); emit \"r\";",
    };

    static const int64_t expected_values[] = {
        6,  // 2 * 3
        14, // 2 * attr
        2,  // 4 / 2
        8,  // attr / 2
        2,  // 5 % 3
        1,  // -5 % 3
        1,  // attr % 2
        5,  // 2 + 3
        9,  // 2 + attr
        -1, // 2 - 3
        -5, // 2 - attr
        2,  // abs
        2,
        7,
        -2, // neg
        7,

        // More complex operations.
        24, // abs(2 * (-5 + attr)
        -1, // attr / 4 - 5
        -2, // attr % 4 - 5

        // Division by 0.
        std::numeric_limits<int64_t>::lowest(),
        std::numeric_limits<int64_t>::lowest(),
    };

    static const int64_t attribute_values[] = {
        0,
        7, // 2 * attr
        0,
        16, // attr / 2
        0,
        0,
        5, // attr % 2
        0,
        7, // 2 + attr
        0,
        2, // attr - 7
        0,
        0,
        -7, // abs(attr)
        0,
        -7, // neg(attr)

        // More complex operations.
        -7, // abs(2 * (-5 + attr)
        16, // attr / 4 - 5
        7,  // attr % 4 - 5

        // Division by 0.
        0,
        0,
    };

    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);
    static const size_t expected_count = sizeof(expected_values) / sizeof(expected_values[0]);
    static const size_t attr_value_count = sizeof(attribute_values) / sizeof(attribute_values[0]);
    ASSERT_TRUE(count == expected_count);
    ASSERT_TRUE(expected_count == attr_value_count);

    auto parser = Parser::create();
    uint16_t prp_id = 0;
    parser->add_property(prp_id, "i");
    uint32_t attr_id = parser->add_attribute("i");

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);

        auto lexer = Lexer::create(scripts[i]);
        hrz::style::Ast full_ast;
        auto a = parser->parse(*lexer, full_ast);
        ASSERT_EQ(Result::Ok, a.type);
        ASSERT_TRUE(a);
        auto optimizer = Optimizer::create({});
        auto ast = std::make_shared<hrz::style::FlatAst>();
        ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

        FeaturesStylingData data;
        data.ast = ast;
        add_repr(data, 0, "r");

        auto attr = new_attribute();
        attr.push(attribute_values[i]);
        data.attributes.insert({attr_id, attr.finalize(0).value()});
        data.feature_count = 1;

        StylingResult resp;
        auto res = hrz_jobs::style_features::run(data, resp, job_context);

        ASSERT_EQ(hrz::JobResult::SUCCESS, res);
        ASSERT_EQ(1, resp.features.instances.size());

        const auto& inst = resp.features.instances.get_data().unsafe_at(0);
        ASSERT_EQ(1, inst.prp_count);
        EXPECT_EQ(prp_id, resp.features.prps.get_data().unsafe_at(inst.first_prp + 0));
        EXPECT_EQ(
            expected_values[i], resp.features.get_values_reader().as_int64(inst.first_prp + 0));
    }
}

TEST_F(FeatureStyling, arithmetic_expressions_floats)
{
    static const char* scripts[] = {
        "set \"i\" = mod(5.5, 2.5); emit \"r\";",
        "set \"i\" = mod(5, 3); emit \"r\";",
        "set \"i\" = mod(-5, 3); emit \"r\";",
        "set \"i\" = mod(attr(\"i\"), 2.5); emit \"r\";",
    };

    static const double expected_values[] = {
        0.5, // 5.5 % 2.5
        2.0, // 5 % 3
        1.0, // -5 % 3
        0.5, // attr("i") % 2.5
    };

    static const double attribute_values[] = {
        0, 0, 0,
        5.5, // attr("i") % 2.5
    };

    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);
    static const size_t expected_count = sizeof(expected_values) / sizeof(expected_values[0]);
    static const size_t attr_value_count = sizeof(attribute_values) / sizeof(attribute_values[0]);
    ASSERT_TRUE(count == expected_count);
    ASSERT_TRUE(expected_count == attr_value_count);

    auto parser = Parser::create();
    uint16_t prp_id = 0;
    parser->add_property(prp_id, "i");
    uint32_t attr_id = parser->add_attribute("i");

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);

        auto lexer = Lexer::create(scripts[i]);
        hrz::style::Ast full_ast;
        auto a = parser->parse(*lexer, full_ast);
        ASSERT_EQ(Result::Ok, a.type);
        ASSERT_TRUE(a);
        auto optimizer = Optimizer::create({});
        auto ast = std::make_shared<hrz::style::FlatAst>();
        ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

        FeaturesStylingData data;
        data.ast = ast;
        add_repr(data, 0, "r");

        auto attr = new_attribute();
        attr.push(attribute_values[i]);
        data.attributes.insert({attr_id, attr.finalize(0).value()});

        data.feature_count = 1;

        StylingResult resp;
        auto res = hrz_jobs::style_features::run(data, resp, job_context);

        ASSERT_EQ(hrz::JobResult::SUCCESS, res);
        ASSERT_EQ(1, resp.features.instances.size());

        const auto& inst = resp.features.instances.get_data().unsafe_at(0);
        ASSERT_EQ(1, inst.prp_count);
        EXPECT_EQ(prp_id, resp.features.prps.get_data().unsafe_at(inst.first_prp + 0));
        EXPECT_EQ(
            expected_values[i], resp.features.get_values_reader().as_number(inst.first_prp + 0));
    }
}

TEST_F(FeatureStyling, branch_exhaustive)
{
    static const char* script =
        "set \"a\" = 0;"
        "if (attr(\"a\") == 1) {"
        "    set \"a\" = 1;"
        "} elif (attr(\"a\") == 2) {"
        "    set \"a\" = 2;"
        "} else {"
        "    set \"a\" = 4;"
        "}"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint16_t prp = 0;
    parser->add_property(prp, "a");
    uint32_t attr_id = parser->add_attribute("a");
    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    auto attr = new_attribute();
    for (int64_t i = 0; i < 5; ++i)
    {
        attr.push(i);
    }
    data.attributes.insert({attr_id, attr.finalize(0).value()});

    data.feature_count = 5;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    int64_t values[5] = {4, 1, 2, 4, 4};
    bool seen[5] = {false, false, false, false, false};

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(5, resp.features.instances.size());

    for (size_t i = 0; i < 5; ++i)
    {
        const auto& inst = resp.features.instances.get_data().unsafe_at(i);
        ASSERT_EQ(1, inst.prp_count);
        size_t index = inst.feature_index;
        EXPECT_EQ(prp, resp.features.prps.get_data().unsafe_at(inst.first_prp + 0));
        EXPECT_EQ(values[index], resp.features.get_values_reader().as_int64(inst.first_prp + 0));
        seen[index] = true;
    }

    for (size_t i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(seen[i]);
    }
}

TEST_F(FeatureStyling, branch_empty_alternatives)
{
    static const char* script =
        "set \"a\" = 0;"
        "if (attr(\"a\") < 45) {"
        "    set \"a\" = 1;"
        "} elif (attr(\"a\") == 2) {"
        "    set \"a\" = 2;"
        "} else {"
        "    set \"a\" = 4;"
        "}"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint16_t prp = 0;
    parser->add_property(prp, "a");
    uint32_t attr_id = parser->add_attribute("a");
    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    auto attr = new_attribute();
    for (int64_t i = 0; i < 5; ++i)
    {
        attr.push(i);
    }
    data.attributes.insert({attr_id, attr.finalize(0).value()});

    data.feature_count = 5;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    int64_t values[5] = {1, 1, 1, 1, 1};
    bool seen[5] = {false, false, false, false, false};

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(5, resp.features.instances.size());

    for (size_t i = 0; i < 5; ++i)
    {
        const auto& inst = resp.features.instances.get_data().unsafe_at(i);
        ASSERT_EQ(1, inst.prp_count);
        size_t index = inst.feature_index;
        EXPECT_EQ(prp, resp.features.prps.get_data().unsafe_at(inst.first_prp + 0));
        EXPECT_EQ(values[index], resp.features.get_values_reader().as_int64(inst.first_prp + 0));
        seen[index] = true;
    }

    for (size_t i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(seen[i]);
    }
}

TEST_F(FeatureStyling, branch_no_branch_taken)
{
    static const char* script =
        "set \"a\" = 0;"
        "if (attr(\"a\") > 45) {"
        "    set \"a\" = 1;"
        "}"
        "if (attr(\"a\") == 78) {"
        "    set \"a\" = 2;"
        "}"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint16_t prp = 0;
    parser->add_property(prp, "a");
    uint32_t attr_id = parser->add_attribute("a");
    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    auto attr = new_attribute();
    for (int64_t i = 0; i < 5; ++i)
    {
        attr.push(i);
    }
    data.attributes.insert({attr_id, attr.finalize(0).value()});

    data.feature_count = 5;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    int64_t values[5] = {0, 0, 0, 0, 0};
    bool seen[5] = {false, false, false, false, false};

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(5, resp.features.instances.size());

    for (size_t i = 0; i < 5; ++i)
    {
        const auto& inst = resp.features.instances.get_data().unsafe_at(i);
        ASSERT_EQ(1, inst.prp_count);
        size_t index = inst.feature_index;
        EXPECT_EQ(prp, resp.features.prps.get_data().unsafe_at(inst.first_prp + 0));
        EXPECT_EQ(values[index], resp.features.get_values_reader().as_int64(inst.first_prp + 0));
        seen[index] = true;
    }

    for (size_t i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(seen[i]);
    }
}

TEST_F(FeatureStyling, branch_nested)
{
    static const char* script =
        "set \"a\" = 0;"
        "if (attr(\"a\") <= 1) {"
        "    if (attr(\"a\") == 0) { set \"a\" = 1; }"
        "    else { set \"a\" = 2; }"
        "}"
        "else {"
        "if (attr(\"a\") >= 3) { set \"a\" = 3; }"
        "}"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint16_t prp = 0;
    parser->add_property(prp, "a");
    uint32_t attr_id = parser->add_attribute("a");
    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    auto attr = new_attribute();
    for (int64_t i = 0; i < 5; ++i)
    {
        attr.push(i);
    }
    data.attributes.insert({attr_id, attr.finalize(0).value()});

    data.feature_count = 5;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    int64_t values[5] = {1, 2, 0, 3, 3};
    bool seen[5] = {false, false, false, false, false};

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(5, resp.features.instances.size());

    for (size_t i = 0; i < 5; ++i)
    {
        const auto& inst = resp.features.instances.get_data().unsafe_at(i);
        ASSERT_EQ(1, inst.prp_count);
        size_t index = inst.feature_index;
        EXPECT_EQ(prp, resp.features.prps.get_data().unsafe_at(inst.first_prp + 0));
        EXPECT_EQ(values[index], resp.features.get_values_reader().as_int64(inst.first_prp + 0));
        seen[index] = true;
    }

    for (size_t i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(seen[i]);
    }
}

TEST_F(FeatureStyling, branch_complex_condition)
{
    static const char* script =
        "set \"a\" = 0;"
        "if (attr(\"b\") < \"six\" and not (attr(\"a\") >= 2 and attr(\"a\") <= 3)) {"
        "    set \"a\" = 1;"
        "}"
        "else {"
        "    if (attr(\"b\") == \"two\" or attr(\"a\") >= 3) {"
        "        set \"a\" = 2;"
        "    } else {"
        "        set \"a\" = 3;"
        "    }"
        "}"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint16_t prp = 0;
    parser->add_property(prp, "a");
    uint32_t attri_id = parser->add_attribute("a");
    uint32_t attrs_id = parser->add_attribute("b");
    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    {
        auto attr = new_attribute();
        for (int64_t i = 0; i < 5; ++i)
        {
            attr.push(i);
        }
        data.attributes.insert({attri_id, attr.finalize(0).value()});
    }

    {
        auto attr = new_attribute();
        attr.push((std::string_view) "one");
        attr.push((std::string_view) "two");
        attr.push((std::string_view) "three");
        attr.push((std::string_view) "four");
        attr.push((std::string_view) "five");
        data.attributes.insert({attrs_id, attr.finalize(0).value()});
    }

    data.feature_count = 5;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    int64_t values[5] = {1, 2, 3, 2, 1};
    bool seen[5] = {false, false, false, false, false};

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(5, resp.features.instances.size());

    for (size_t i = 0; i < 5; ++i)
    {
        const auto& inst = resp.features.instances.get_data().unsafe_at(i);
        ASSERT_EQ(1, inst.prp_count);
        size_t index = inst.feature_index;
        EXPECT_EQ(prp, resp.features.prps.get_data().unsafe_at(inst.first_prp + 0));
        EXPECT_EQ(values[index], resp.features.get_values_reader().as_int64(inst.first_prp + 0));
        seen[index] = true;
    }

    for (size_t i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(seen[i]);
    }
}

TEST_F(FeatureStyling, branch_with_termination)
{
    static const char* script =
        "if (attr(\"a\") == 1) {"
        "    emit \"a\";"
        "} elif (attr(\"a\") == 2) {"
        "    discard;"
        "} else {"
        "    if (attr(\"a\") == 3) {"
        "        emit \"b\";"
        "    }"
        "}"
        "emit \"c\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t repr_a = 1;
    uint32_t repr_b = 2;
    uint32_t repr_c = 3;
    uint32_t attr_id = parser->add_attribute("a");
    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, repr_a, "a");
    add_repr(data, repr_b, "b");
    add_repr(data, repr_c, "c");

    auto attr = new_attribute();
    for (int64_t i = 0; i < 5; ++i)
    {
        attr.push(i);
    }
    data.attributes.insert({attr_id, attr.finalize(0).value()});

    data.feature_count = 5;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    uint32_t repr_id[5] = {repr_c, repr_a, 0, repr_b, repr_c};
    bool seen[5] = {false, false, false, false, false};

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(4, resp.features.instances.size());

    for (size_t i = 0; i < 4; ++i)
    {
        size_t index = resp.features.instances.get_data().unsafe_at(i).feature_index;
        EXPECT_EQ(repr_id[index], resp.features.instances.get_data().unsafe_at(i).repr_id);
        seen[index] = true;
    }

    ASSERT_TRUE(seen[0]);
    ASSERT_TRUE(seen[1]);
    ASSERT_FALSE(seen[2]);
    ASSERT_TRUE(seen[3]);
    ASSERT_TRUE(seen[4]);
}

TEST_F(FeatureStyling, branch_with_termination_double)
{
    static const char* script =
        "if (attr(\"height\") < 20) {"
        "    set \"extrusion\" = attr(\"height\");"
        "    emit \"repr\";"
        "}";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t repr = 0;
    uint32_t attr_id = parser->add_attribute("height");
    /*int prp_id =*/parser->add_property(0, "extrusion");
    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, repr, "repr");

    auto attr = new_attribute();
    attr.push(94.0);
    attr.push(12.0);
    attr.push(20.0);
    attr.push(39.0);
    attr.push(-98.0);
    data.attributes.insert({attr_id, attr.finalize(0).value()});

    data.feature_count = 5;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    bool seen[5] = {false, false, false, false, false};

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(2, resp.features.instances.size());

    for (size_t i = 0; i < 2; ++i)
    {
        size_t index = resp.features.instances.get_data().unsafe_at(i).feature_index;
        EXPECT_EQ(repr, resp.features.instances.get_data().unsafe_at(i).repr_id);
        EXPECT_EQ(1, resp.features.instances.get_data().unsafe_at(i).prp_count);
        seen[index] = true;
    }

    ASSERT_FALSE(seen[0]);
    ASSERT_TRUE(seen[1]);
    ASSERT_FALSE(seen[2]);
    ASSERT_FALSE(seen[3]);
    ASSERT_TRUE(seen[4]);
}

TEST_F(FeatureStyling, lots_of_features)
{
    static const char* script =
        "if (attr(\"height\") < 20) {"
        "    emit \"repr\";"
        "}";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t attr_id = parser->add_attribute("height");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "repr");

    auto attr = new_attribute();
    size_t expected_pass = 0;
    for (int i = 0; i < 300; ++i)
    {
        if (i % 3 == 0)
        {
            attr.push(94.0);
        }
        else
        {
            attr.push(12.0);
            expected_pass += 1;
        }
    }
    data.attributes.insert({attr_id, attr.finalize(0).value()});

    data.feature_count = 300;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(expected_pass, resp.features.instances.size());

    hrz::flat_hash_set<uint32_t> indices;

    for (size_t i = 0; i < expected_pass; ++i)
    {
        indices.insert(resp.features.instances.get_data().unsafe_at(i).feature_index);
        EXPECT_NE(resp.features.instances.get_data().unsafe_at(i).feature_index % 3, 0);
    }

    EXPECT_EQ(indices.size(), expected_pass);
}

TEST_F(FeatureStyling, fork_after_discard)
{
    static const char* script =
        "if (attr(\"height\") < 20) {                   "
        "    discard;                                   "
        "}                                              "
        "set \"extrusion\" = attr(\"height\");          "
        "fork {                                         "
        "    set \"extrusion\" = 100.0;                 "
        "    emit \"r1\";                               "
        "}                                              "
        "emit \"r2\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t r1 = 1;
    uint32_t r2 = 2;
    uint32_t attr_id = parser->add_attribute("height");
    /*int prp_id =*/parser->add_property(0, "extrusion");
    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, r1, "r1");
    add_repr(data, r2, "r2");

    auto attr = new_attribute();
    attr.push(94.0);
    attr.push(12.0);
    attr.push(20.0);
    attr.push(39.0);
    attr.push(-98.0);
    data.attributes.insert({attr_id, attr.finalize(0).value()});

    data.feature_count = 5;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(6, resp.features.instances.size());

    bool seen[10] = {false, false, false, false, false, false, false, false, false, false};

    for (size_t i = 0; i < 6; ++i)
    {
        size_t index = resp.features.instances.get_data().unsafe_at(i).feature_index;
        uint32_t repr = resp.features.instances.get_data().unsafe_at(i).repr_id;

        EXPECT_TRUE(repr == r1 || repr == r2);
        EXPECT_EQ(1, resp.features.instances.get_data().unsafe_at(i).prp_count);

        if (repr == r1)
        {
            seen[index] = true;
        }
        else if (repr == r2)
        {
            seen[index + 5] = true;
        }
    }

    EXPECT_TRUE(seen[0]);
    EXPECT_FALSE(seen[1]);
    EXPECT_TRUE(seen[2]);
    EXPECT_TRUE(seen[3]);
    EXPECT_FALSE(seen[4]);

    EXPECT_TRUE(seen[5]);
    EXPECT_FALSE(seen[6]);
    EXPECT_TRUE(seen[7]);
    EXPECT_TRUE(seen[8]);
    EXPECT_FALSE(seen[9]);
}

TEST_F(FeatureStyling, fork_in_if)
{
    static const char* script =
        "if (attr(\"height\") < 20) {                   "
        "    fork { emit \"r1\"; }                      "
        "}                                              "
        "emit \"r2\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t r1 = 1;
    uint32_t r2 = 2;
    uint32_t attr_id = parser->add_attribute("height");
    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, r1, "r1");
    add_repr(data, r2, "r2");

    auto attr = new_attribute();
    attr.push(94.0);
    attr.push(12.0);
    attr.push(20.0);
    attr.push(39.0);
    attr.push(-98.0);
    data.attributes.insert({attr_id, attr.finalize(0).value()});

    data.feature_count = 5;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(7, resp.features.instances.size());

    bool seen[10] = {false, false, false, false, false, false, false, false, false, false};

    for (size_t i = 0; i < 7; ++i)
    {
        size_t index = resp.features.instances.get_data().unsafe_at(i).feature_index;
        uint32_t repr = resp.features.instances.get_data().unsafe_at(i).repr_id;

        EXPECT_TRUE(repr == r1 || repr == r2);

        if (repr == r1)
        {
            seen[index] = true;
        }
        else if (repr == r2)
        {
            seen[index + 5] = true;
        }
    }

    EXPECT_FALSE(seen[0]);
    EXPECT_TRUE(seen[1]);
    EXPECT_FALSE(seen[2]);
    EXPECT_FALSE(seen[3]);
    EXPECT_TRUE(seen[4]);

    EXPECT_TRUE(seen[5]);
    EXPECT_TRUE(seen[6]);
    EXPECT_TRUE(seen[7]);
    EXPECT_TRUE(seen[8]);
    EXPECT_TRUE(seen[9]);
}

// From https://redacted.localhost/browse/HRZ-508
TEST_F(FeatureStyling, emit_in_nested_if)
{
    static const char* script =
        "if(attr(\"geometry_type\") == 1) {             "
        "    if(attr(\"is_clamped\") == 1) {            "
        "        emit \"flat-line\";                    "
        "    }                                          "
        "}                                              "
        "elif(attr(\"geometry_type\") == 2) {           "
        "    emit \"flat\";                             "
        "}";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t r1 = 12;
    uint32_t r2 = 18;
    uint32_t attr1_id = parser->add_attribute("geometry_type");
    uint32_t attr2_id = parser->add_attribute("is_clamped");
    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, r1, "flat");
    add_repr(data, r2, "flat-line");

    // geometry_type
    auto attr1 = new_attribute();
    attr1.push((uint64_t)1);
    attr1.push((uint64_t)2);
    data.attributes.insert({attr1_id, attr1.finalize(0).value()});

    // is_clamped
    auto attr2 = new_attribute();
    attr2.push((uint64_t)1);
    attr2.push((uint64_t)1);
    data.attributes.insert({attr2_id, attr2.finalize(0).value()});

    data.feature_count = 2;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(2, resp.features.instances.size());

    bool seen[4] = {false, false, false, false};

    for (size_t i = 0; i < 2; ++i)
    {
        size_t index = resp.features.instances.get_data().unsafe_at(i).feature_index;
        uint32_t repr = resp.features.instances.get_data().unsafe_at(i).repr_id;

        EXPECT_TRUE(repr == r1 || repr == r2);

        if (repr == r1)
        {
            seen[index] = true;
        }
        else if (repr == r2)
        {
            seen[index + 2] = true;
        }
    }

    EXPECT_FALSE(seen[0]);
    EXPECT_TRUE(seen[1]);

    EXPECT_TRUE(seen[2]);
    EXPECT_FALSE(seen[3]);
}

TEST_F(FeatureStyling, operators_type_cast)
{
    static const char* script =
        "set \"prp_1_dbl\" = min(attr(\"attr_dbl\"), 50);  "
        "set \"prp_2_dbl\" = min(attr(\"attr_int\"), 50);  "
        "set \"prp_3_dbl\" = min(attr(\"attr_uint\"), 50);  "
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    /*int prp_1_dbl =*/parser->add_property(0, "prp_1_dbl");
    /*int prp_2_dbl =*/parser->add_property(1, "prp_2_dbl");
    /*int prp_3_dbl =*/parser->add_property(2, "prp_3_dbl");
    uint32_t attr_dbl = parser->add_attribute("attr_dbl");
    uint32_t attr_int = parser->add_attribute("attr_int");
    uint32_t attr_uint = parser->add_attribute("attr_uint");
    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    {
        auto attr = new_attribute();
        attr.push(30.0);
        attr.push(60.0);
        data.attributes.insert({attr_dbl, attr.finalize(0).value()});
    }

    {
        auto attr = new_attribute();
        attr.push((int64_t)30);
        attr.push((int64_t)60);
        data.attributes.insert({attr_int, attr.finalize(0).value()});
    }

    {
        auto attr = new_attribute();
        attr.push((uint64_t)30);
        attr.push((uint64_t)60);
        data.attributes.insert({attr_uint, attr.finalize(0).value()});
    }

    data.feature_count = 2;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(2, resp.features.instances.size());
    ASSERT_EQ(6, resp.features.values.size());

    EXPECT_EQ(resp.features.get_values_reader().as_number(0), 30.0);
    EXPECT_EQ(resp.features.get_values_reader().as_number(1), 30.0);
    EXPECT_EQ(resp.features.get_values_reader().as_number(2), 30.0);
    EXPECT_EQ(resp.features.get_values_reader().as_number(3), 50.0);
    EXPECT_EQ(resp.features.get_values_reader().as_number(4), 50.0);
    EXPECT_EQ(resp.features.get_values_reader().as_number(5), 50.0);
}

TEST_F(FeatureStyling, mapbox_typeof)
{
    static const char* script =
        "set \"type\" = mapbox_typeof(attr(\"a\"));"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "type");
    uint32_t attr_id = parser->add_attribute("a");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);

    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    {
        auto attr = new_attribute();
        attr.push_null();
        attr.push(true);
        attr.push(false);
        attr.push(INT64_C(3));
        attr.push(60.5);
        attr.push(UINT64_C(0xffff'ffff'ffff'ffff));
        attr.push(std::string_view("hello"));
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = 7;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(7, resp.features.instances.size());
    ASSERT_EQ(7, resp.features.values.size());

    EXPECT_EQ(resp.features.get_values_reader().as_string(0), "null");
    EXPECT_EQ(resp.features.get_values_reader().as_string(1), "boolean");
    EXPECT_EQ(resp.features.get_values_reader().as_string(2), "boolean");
    EXPECT_EQ(resp.features.get_values_reader().as_string(3), "number");
    EXPECT_EQ(resp.features.get_values_reader().as_string(4), "number");
    EXPECT_EQ(resp.features.get_values_reader().as_string(5), "number");
    EXPECT_EQ(resp.features.get_values_reader().as_string(6), "string");
}

TEST_F(FeatureStyling, to_color)
{
    static const char* script =
        "set \"type\" = to_color(attr(\"a\"));"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "type");
    uint32_t attr_id = parser->add_attribute("a");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);

    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    {
        auto attr = new_attribute();
        attr.push(true);
        attr.push(false);
        attr.push(INT64_C(3));
        attr.push(60.5);
        attr.push(std::string_view("red"));
        attr.push(std::string_view("#1234"));
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = 6;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(6, resp.features.instances.size());
    ASSERT_EQ(6, resp.features.values.size());

    EXPECT_EQ(resp.features.get_values_reader().as_number(0), 1);
    EXPECT_EQ(resp.features.get_values_reader().as_number(1), 0);
    EXPECT_EQ(resp.features.get_values_reader().as_number(2), 3);
    EXPECT_EQ(resp.features.get_values_reader().as_number(3), 60);
    EXPECT_EQ(resp.features.get_values_reader().as_number(4), 0xff0000ff);
    EXPECT_EQ(resp.features.get_values_reader().as_number(5), 0x44332211);
}

TEST_F(FeatureStyling, to_int)
{
    static const char* script =
        "set \"type\" = to_int(attr(\"a\"));"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "type");
    uint32_t attr_id = parser->add_attribute("a");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);

    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    {
        auto attr = new_attribute();
        attr.push(true);
        attr.push(false);
        attr.push(INT64_C(3));
        attr.push(60.5);
        attr.push(std::string_view("78"));
        attr.push(std::string_view("   -56  "));
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = 6;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(6, resp.features.instances.size());
    ASSERT_EQ(6, resp.features.values.size());

    EXPECT_EQ(resp.features.get_values_reader().as_number(0), 1);
    EXPECT_EQ(resp.features.get_values_reader().as_number(1), 0);
    EXPECT_EQ(resp.features.get_values_reader().as_number(2), 3);
    EXPECT_EQ(resp.features.get_values_reader().as_number(3), 60);
    EXPECT_EQ(resp.features.get_values_reader().as_number(4), 78);
    EXPECT_EQ(resp.features.get_values_reader().as_number(5), -56);
}

TEST_F(FeatureStyling, to_uint)
{
    static const char* script =
        "set \"type\" = to_uint(attr(\"a\"));"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "type");
    uint32_t attr_id = parser->add_attribute("a");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);

    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    {
        auto attr = new_attribute();
        attr.push(true);
        attr.push(false);
        attr.push(INT64_C(3));
        attr.push(60.5);
        attr.push(std::string_view("78"));
        attr.push(std::string_view("   -56  "));
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = 6;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(6, resp.features.instances.size());
    ASSERT_EQ(6, resp.features.values.size());

    EXPECT_EQ(resp.features.get_values_reader().as_number(0), 1);
    EXPECT_EQ(resp.features.get_values_reader().as_number(1), 0);
    EXPECT_EQ(resp.features.get_values_reader().as_number(2), 3);
    EXPECT_EQ(resp.features.get_values_reader().as_number(3), 60);
    EXPECT_EQ(resp.features.get_values_reader().as_number(4), 78);
    EXPECT_EQ(resp.features.get_values_reader().as_number(5), 0);
}

TEST_F(FeatureStyling, to_number)
{
    static const char* script =
        "set \"type\" = to_number(attr(\"a\"));"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "type");
    uint32_t attr_id = parser->add_attribute("a");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);

    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    {
        auto attr = new_attribute();
        attr.push(true);
        attr.push(false);
        attr.push(INT64_C(3));
        attr.push(60.5);
        attr.push(std::string_view("78.89"));
        attr.push(std::string_view("   -56  "));
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = 6;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(6, resp.features.instances.size());
    ASSERT_EQ(6, resp.features.values.size());

    EXPECT_EQ(resp.features.get_values_reader().as_number(0), 1);
    EXPECT_EQ(resp.features.get_values_reader().as_number(1), 0);
    EXPECT_EQ(resp.features.get_values_reader().as_number(2), 3);
    EXPECT_EQ(resp.features.get_values_reader().as_number(3), 60.5);
    EXPECT_EQ(resp.features.get_values_reader().as_number(4), 78.89);
    EXPECT_EQ(resp.features.get_values_reader().as_number(5), -56);
}

TEST_F(FeatureStyling, to_string)
{
    static const char* script =
        "set \"type\" = to_string(attr(\"a\"));"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "type");
    uint32_t attr_id = parser->add_attribute("a");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);

    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    {
        auto attr = new_attribute();
        attr.push(true);
        attr.push(false);
        attr.push(INT64_C(3));
        attr.push(60.5);
        attr.push(std::string_view("poop"));
        attr.push(std::string_view("   -56  "));
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = 6;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(6, resp.features.instances.size());
    ASSERT_EQ(6, resp.features.values.size());

    EXPECT_EQ(resp.features.get_values_reader().as_string(0), "true");
    EXPECT_EQ(resp.features.get_values_reader().as_string(1), "false");
    EXPECT_EQ(resp.features.get_values_reader().as_string(2), "3");
    EXPECT_EQ(resp.features.get_values_reader().as_string(3), "60.5");
    EXPECT_EQ(resp.features.get_values_reader().as_string(4), "poop");
    EXPECT_EQ(resp.features.get_values_reader().as_string(5), "   -56  ");
}

TEST_F(FeatureStyling, to_boolean)
{
    static const char* script =
        "set \"type\" = to_boolean(attr(\"a\"));"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "type");
    uint32_t attr_id = parser->add_attribute("a");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);

    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    {
        auto attr = new_attribute();
        attr.push(true);
        attr.push(false);
        attr.push(INT64_C(3));
        attr.push(0.0);
        attr.push(std::string_view(" TRue "));
        attr.push(std::string_view("   1  "));
        attr.push(std::string_view("yes"));
        attr.push_null();
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = 8;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(data.feature_count, resp.features.instances.size());
    ASSERT_EQ(data.feature_count, resp.features.values.size());

    for (size_t i = 0; i < data.feature_count; ++i)
    {
        EXPECT_TRUE(attr_is_bool(resp.features.get_values_reader().as_ref(i)));
    }

    EXPECT_EQ(resp.features.get_values_reader().as_bool(0), true);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(1), false);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(2), true);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(3), false);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(4), true);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(5), true);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(6), false);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(7), false);
}

TEST_F(FeatureStyling, is_null)
{
    static const char* script =
        "set \"type\" = is_null(attr(\"a\"));"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "type");
    uint32_t attr_id = parser->add_attribute("a");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);

    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    {
        auto attr = new_attribute();
        attr.push(true);
        attr.push(false);
        attr.push(0.0);
        attr.push_null();
        attr.push(std::string_view(""));
        attr.push(std::string_view("null"));
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = 6;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(data.feature_count, resp.features.instances.size());
    ASSERT_EQ(data.feature_count, resp.features.values.size());

    for (size_t i = 0; i < data.feature_count; ++i)
    {
        EXPECT_TRUE(attr_is_bool(resp.features.get_values_reader().as_ref(i)));
    }

    EXPECT_EQ(resp.features.get_values_reader().as_bool(0), false);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(1), false);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(2), false);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(3), true);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(4), false);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(5), false);
}

TEST_F(FeatureStyling, is_nan)
{
    static const char* script =
        "set \"result\" = is_nan(attr(\"a\"));"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "result");
    uint32_t attr_id = parser->add_attribute("a");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);

    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    {
        auto attr = new_attribute();
        attr.push(false);
        attr.push(-48.24);
        attr.push(std::numeric_limits<double>::quiet_NaN());
        attr.push_null();
        attr.push(std::string_view("zeuizjefoizfejf"));
        attr.push(std::string_view("0.5"));
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = 6;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(data.feature_count, resp.features.instances.size());
    ASSERT_EQ(data.feature_count, resp.features.values.size());

    for (size_t i = 0; i < data.feature_count; ++i)
    {
        EXPECT_TRUE(attr_is_bool(resp.features.get_values_reader().as_ref(i)));
    }

    EXPECT_EQ(resp.features.get_values_reader().as_bool(0), false);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(1), false);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(2), true);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(3), true);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(4), true);
    EXPECT_EQ(resp.features.get_values_reader().as_bool(5), true);
}

TEST_F(FeatureStyling, value_or)
{
    static const char* script =
        "set \"r1\" = value_or(attr(\"a\"), 1000.0);"
        "set \"r2\" = value_or(attr(\"a\"), \"str\");"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "r1");
    parser->add_property(1, "r2");
    uint32_t attr_id = parser->add_attribute("a");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);

    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    {
        auto attr = new_attribute();
        attr.push_null();
        attr.push("hello");
        attr.push(0.0);
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = 3;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(data.feature_count, resp.features.instances.size());
    ASSERT_EQ(data.feature_count * 2, resp.features.values.size());

    OwnedAttributeValue expected[3][2] = {
        {attr_from<OwnedAttributeValue>(1000.0), attr_from<OwnedAttributeValue>("str")},
        {attr_from<OwnedAttributeValue>("hello"), attr_from<OwnedAttributeValue>("hello")},
        {attr_from<OwnedAttributeValue>(0.0), attr_from<OwnedAttributeValue>(0.0)}};

    for (size_t i = 0; i < data.feature_count; ++i)
    {
        auto f = resp.features.instances.get_cdata().unsafe_at(i);
        for (size_t j = 0; j < f.prp_count; ++j)
        {
            auto prp = resp.features.prps.get_data().unsafe_at(f.first_prp + j);
            EXPECT_EQ(
                resp.features.get_values_reader().as_ref(f.first_prp + j),
                attr_as_ref(expected[f.feature_index][prp]));
        }
    }
}

TEST_F(FeatureStyling, large_batch)
{
    static const char* script =
        "set \"prp1\" = add(mul(attr(\"attr\"), 10), 5);"
        "set \"prp2\" = max(min(attr(\"attr\"), 1000), 100);"
        "set \"prp3\" = div(mul(9, 20), 3);"
        "emit \"r\";";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});

    uint16_t prp1_id = 0;
    parser->add_property(prp1_id, "prp1");

    uint16_t prp2_id = 1;
    parser->add_property(prp2_id, "prp2");

    uint16_t prp3_id = 2;
    parser->add_property(prp3_id, "prp3");

    uint32_t attr_id = parser->add_attribute("attr");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    size_t count = 2000;

    {
        auto attr = new_attribute();
        for (uint64_t i = 0; i < count; ++i)
        {
            attr.push(i);
        }
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = count;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(count, resp.features.instances.size());
    ASSERT_EQ(count * 3, resp.features.values.size());

    for (const auto& inst : resp.features.instances.get_data())
    {
        size_t index = inst.feature_index;
        uint64_t prp1 = index * 10 + 5;
        uint64_t prp2 = std::max(std::min(index, (uint64_t)1000), (uint64_t)100);
        ASSERT_EQ(3, inst.prp_count);

        int seen1 = 0, seen2 = 0, seen3 = 0;

        for (unsigned int i = 0; i < inst.prp_count; ++i)
        {
            int prp_id = resp.features.prps.get_data().unsafe_at(inst.first_prp + i);
            uint64_t value = resp.features.get_values_reader().as_uint64(inst.first_prp + i);
            if (prp_id == prp1_id)
            {
                EXPECT_EQ(prp1, value);
                seen1 += 1;
            }
            else if (prp_id == prp2_id)
            {
                EXPECT_EQ(prp2, value);
                seen2 += 1;
            }
            else if (prp_id == prp3_id)
            {
                EXPECT_EQ(60, value);
                seen3 += 1;
            }
            else
            {
                FAIL();
            }
        }

        EXPECT_EQ(1, seen1);
        EXPECT_EQ(1, seen2);
        EXPECT_EQ(1, seen3);
    }
}

TEST_F(FeatureStyling, dynamic_emit_string)
{
    static const char* script = "emit attr(\"repr_name\");";
    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t hello_repr = 100001;
    uint32_t world_repr = 100002;
    uint32_t attr_id = parser->add_attribute("repr_name");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, hello_repr, "hello");
    add_repr(data, world_repr, "world");

    {
        auto attr = new_attribute();
        attr.push((std::string_view) "hello");
        attr.push((std::string_view) "world");
        attr.push((std::string_view) "hello");
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = 3;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(3, resp.features.instances.size());

    int seen0 = 0;
    int seen1 = 0;
    int seen2 = 0;

    for (const auto& inst : resp.features.instances.get_data())
    {
        if (inst.feature_index == 0)
        {
            EXPECT_EQ(hello_repr, inst.repr_id);
            seen0 += 1;
        }
        else if (inst.feature_index == 1)
        {
            EXPECT_EQ(world_repr, inst.repr_id);
            seen1 += 1;
        }
        else if (inst.feature_index == 2)
        {
            EXPECT_EQ(hello_repr, inst.repr_id);
            seen2 += 1;
        }
        else
        {
            FAIL();
        }
    }

    EXPECT_EQ(1, seen0);
    EXPECT_EQ(1, seen1);
    EXPECT_EQ(1, seen2);
}

TEST_F(FeatureStyling, dynamic_emit_string_unknown_repr)
{
    static const char* script = "emit attr(\"repr_name\");";
    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t world_repr = 99;
    uint32_t attr_id = parser->add_attribute("repr_name");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 12, "hello");
    add_repr(data, world_repr, "world");

    {
        auto attr = new_attribute();
        attr.push((std::string_view) "hullo");
        attr.push((std::string_view) "world");
        attr.push((std::string_view) "hullo");
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = 3;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(1, resp.features.instances.size());

    int seen0 = 0;
    int seen1 = 0;
    int seen2 = 0;

    for (const auto& inst : resp.features.instances.get_data())
    {
        if (inst.feature_index == 0)
        {
            seen0 += 1;
        }
        else if (inst.feature_index == 1)
        {
            EXPECT_EQ(world_repr, inst.repr_id);
            seen1 += 1;
        }
        else if (inst.feature_index == 2)
        {
            seen2 += 1;
        }
        else
        {
            FAIL();
        }
    }

    EXPECT_EQ(0, seen0);
    EXPECT_EQ(1, seen1);
    EXPECT_EQ(0, seen2);
}

TEST_F(FeatureStyling, dynamic_emit_id)
{
    static const char* script = "emit add(neg(attr(\"repr_id\")), 2);";
    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    uint32_t repr_0 = 0;
    uint32_t repr_1 = 1;
    uint32_t repr_2 = 2;
    uint32_t attr_id = parser->add_attribute("repr_id");

    hrz::style::Ast full_ast;
    auto a = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, a.type);
    ASSERT_TRUE(a);
    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, repr_0, "hello");
    add_repr(data, repr_1, "world");
    add_repr(data, repr_2, ":)");

    {
        auto attr = new_attribute();
        attr.push((uint64_t)1);
        attr.push((uint64_t)0);
        attr.push((uint64_t)2);
        data.attributes.insert({attr_id, attr.finalize(0).value()});
    }

    data.feature_count = 3;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(3, resp.features.instances.size());

    int seen0 = 0;
    int seen1 = 0;
    int seen2 = 0;

    for (const auto& inst : resp.features.instances.get_data())
    {
        if (inst.feature_index == 0)
        {
            EXPECT_EQ(repr_1, inst.repr_id);
            seen0 += 1;
        }
        else if (inst.feature_index == 1)
        {
            EXPECT_EQ(repr_2, inst.repr_id);
            seen1 += 1;
        }
        else if (inst.feature_index == 2)
        {
            EXPECT_EQ(repr_0, inst.repr_id);
            seen2 += 1;
        }
        else
        {
            FAIL();
        }
    }

    EXPECT_EQ(1, seen0);
    EXPECT_EQ(1, seen1);
    EXPECT_EQ(1, seen2);
}

TEST_F(FeatureStyling, rand_no_ids)
{
    static const char* script =
        "set \"v0\" = rand_unif_i(0, 1000);"
        "set \"v1\" = rand_unif_i(0, 1000);"
        "set \"v2\" = rand_unif_i(0, 1000);"
        "emit 0;";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "v0");
    parser->add_property(1, "v1");
    parser->add_property(2, "v2");

    hrz::style::Ast full_ast;
    auto parse_res = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, parse_res.type);
    ASSERT_TRUE(parse_res);

    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    data.feature_count = 4;
    data.rng_seed = 0;

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(4, resp.features.instances.size());
    ASSERT_EQ(12, resp.features.values.size());

    int64_t values[4][3] = {{8, 374, 547}, {737, 413, 701}, {417, 388, 19}, {849, 360, 844}};

    for (const auto& inst : resp.features.instances.get_data())
    {
        ASSERT_EQ(3, inst.prp_count);
        for (unsigned int i = 0; i < inst.prp_count; ++i)
        {
            int prp_id = resp.features.prps.get_data().unsafe_at(inst.first_prp + i);
            auto value = resp.features.get_values_reader().as_int64(inst.first_prp + i);
            EXPECT_EQ(values[inst.feature_index][prp_id], value);
        }
    }
}

TEST_F(FeatureStyling, rand_with_ids)
{
    static const char* script =
        "set \"v0\" = rand_unif_i(0, 1000);"
        "set \"v1\" = rand_unif_i(0, 1000);"
        "set \"v2\" = rand_unif_i(0, 1000);"
        "emit 0;";

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(0, "v0");
    parser->add_property(1, "v1");
    parser->add_property(2, "v2");

    hrz::style::Ast full_ast;
    auto parse_res = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, parse_res.type);
    ASSERT_TRUE(parse_res);

    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    hrz::BlobVector<FeatureIdHash> id_hashes(blob_allocator);
    id_hashes.push_back(45);
    id_hashes.push_back(67);
    id_hashes.push_back(67);
    id_hashes.push_back(45);

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    data.feature_count = 4;
    data.rng_seed = 0;
    data.feature_ids_hashes = id_hashes.to_blob_array().value();

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(4, resp.features.instances.size());
    ASSERT_EQ(12, resp.features.values.size());

    int64_t values[4][3] = {{354, 625, 23}, {968, 577, 621}, {968, 577, 621}, {354, 625, 23}};

    for (const auto& inst : resp.features.instances.get_data())
    {
        ASSERT_EQ(3, inst.prp_count);
        for (unsigned int i = 0; i < inst.prp_count; ++i)
        {
            int prp_id = resp.features.prps.get_data().unsafe_at(inst.first_prp + i);
            auto value = resp.features.get_values_reader().as_int64(inst.first_prp + i);
            EXPECT_EQ(values[inst.feature_index][prp_id], value);
        }
    }
}

// This test checks that no matter the order of the features, the same
// random values are generated for the same feature ids.
// See HRZ-1241.
TEST_F(FeatureStyling, rand_in_condition)
{
    static const char* script =
        "if (rand_unif_u(0, 2) == 0) { set \"v\" = rand_unif_i(1000, 2000); }"
        "else { set \"v\" = rand_unif_u(0, 1000); }"
        "emit 0;";

    static constexpr int kValueProperty = 1;

    auto lexer = Lexer::create(script);
    auto parser = Parser::create();
    auto optimizer = Optimizer::create({});
    parser->add_property(kValueProperty, "v");

    hrz::style::Ast full_ast;
    auto parse_res = parser->parse(*lexer, full_ast);
    ASSERT_EQ(Result::Ok, parse_res.type);
    ASSERT_TRUE(parse_res);

    auto ast = std::make_shared<hrz::style::FlatAst>();
    ASSERT_TRUE(optimizer->optimize(std::move(full_ast), ast.get()));

    hrz::BlobVector<FeatureIdHash> id_hashes(blob_allocator);
    for (int i = 0; i < 8; ++i)
    {
        id_hashes.push_back(i);
    }

    const int shuffled_ids[8] = {4, 7, 0, 1, 5, 6, 2, 3};

    hrz::BlobVector<FeatureIdHash> id_hashes2(blob_allocator);
    for (int i = 0; i < 8; ++i)
    {
        id_hashes2.push_back(shuffled_ids[i]);
    }

    FeaturesStylingData data;
    data.ast = ast;
    add_repr(data, 0, "r");

    data.feature_count = 8;
    data.rng_seed = 0;
    data.feature_ids_hashes = id_hashes.to_blob_array().value();

    StylingResult resp;
    auto res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(8, resp.features.instances.size());
    ASSERT_EQ(8, resp.features.values.size());

    int64_t result[8];
    auto values_reader = resp.features.get_values_reader();
    auto instances = resp.features.instances.get_cdata();
    for (const auto& instance : instances)
    {
        result[instance.feature_index] = values_reader.as_int64(instance.first_prp);
    }

    data.feature_count = 8;
    data.rng_seed = 0;
    data.feature_ids_hashes = id_hashes2.to_blob_array().value();

    res = hrz_jobs::style_features::run(data, resp, job_context);

    ASSERT_EQ(hrz::JobResult::SUCCESS, res);
    ASSERT_EQ(8, resp.features.instances.size());
    ASSERT_EQ(8, resp.features.values.size());

    values_reader = resp.features.get_values_reader();
    instances = resp.features.instances.get_cdata();

    for (const auto& instance : instances)
    {
        EXPECT_EQ(
            result[shuffled_ids[instance.feature_index]],
            values_reader.as_int64(instance.first_prp));
    }
}

} // namespace
