#include "mycelium/render_graph.h"

#include "mycelium/backend.h"

#include <gtest/gtest.h>

#include <memory>
#include <span>

class DummyInstance : public my::Instance
{
    Info _info;
    uint64_t _resource_id = 1;

public:
    void configure_shaders_linking(const ShadersLinkingConfig&) override{};

    void advance_shaders_link(bool idle = false) override {}

    void add_global_shader_define(const char* name, const char* value = "") override {}

    const Info& get_info() const override { return _info; }

    size_t get_uniform_buffer_offset_alignment() const override { return 256; }

    bool is_texture_format_available(my::TextureFormat) const override { return false; }

    bool is_texture_download_ready(uint64_t id) const override { return false; }

    void cancel_texture_download(uint64_t id) override {}

    my::TextureDownloadData retrieve_texture_download(uint64_t id) override { return {}; }

    my::QueryTimeResult retrieve_elapsed_time(uint64_t id, my::QueryTimeStatus& status) override
    {
        return {};
    }

    void delete_query(uint64_t id) override{};
    void activate_resource_allocation_reports() override{};
    void deactivate_resource_allocation_reports() override{};

    size_t get_resource_allocation_report_count() const override { return 0; };

    const my::ResourceAllocationReport* get_resource_allocation_reports() override
    {
        return nullptr;
    };

    void clear_resource_allocation_reports() override{};
    void set_memory_limit(uint64_t size_bytes) override{};

    GpuMemoryInfo get_memory_info() override { return {}; }

    ShadersInfo get_shaders_info() const override { return {}; }

    bool begin_frame() override { return true; }

    void end_frame() override {}

    // Resource context

    my::ResourceHandle alloc(const my::Resource*) override
    {
        return my::ResourceHandle{_resource_id++};
    }

    void dealloc(my::ResourceHandle) override {}

    void realloc_buffer(my::ResourceHandle, const my::BufferResource*) override {}

    void update_texture_layout(my::ResourceHandle, const my::TextureLayout&) override {}

    void update_renderbuffer_size(my::ResourceHandle, uint32_t w, uint32_t h) override {}

    my::ResourceHandle retrieve_shader(const char* name) const override
    {
        return my::ResourceHandle::null();
    }

    // Render context

    void clear(uint32_t clear_count, const my::ClearTarget* values) override {}

    void set_viewport(const my::ViewportState&) override {}

    void set_framebuffer(my::ResourceHandle fbo, const my::ViewportState&) override {}

    void update_buffer(my::ResourceHandle buffer, size_t offset, size_t size, const void* data)
        override
    {
    }

    void update_texture(
        my::ResourceHandle texture,
        my::TextureFormat format,
        int level,
        uint32_t x,
        uint32_t y,
        uint32_t z,
        uint32_t w,
        uint32_t h,
        uint32_t d,
        std::span<const std::byte> data,
        TextureUpdateDataLayout) override
    {
    }

    void draw(
        const my::DrawBatchInfo& info,
        my::ResourceHandle shader,
        my::ResourceHandle vertex_input,
        uint32_t ubo_count,
        const my::UboBinding* ubos,
        uint32_t texture_count,
        const my::TextureBinding* textures) override
    {
    }

    void blit_framebuffers(
        my::ResourceHandle src,
        my::Rect src_rect,
        my::Rect dst_rect,
        my::AspectFlags,
        my::Attachment smy_attachment,
        uint32_t dst_attachment_count,
        const my::Attachment* dst_attachments,
        my::SamplerParams::Filter) override
    {
    }

    my::TextureDownloadData color_texture_download_sync(
        my::ResourceHandle framebuffer,
        my::Attachment color_attachment,
        my::Rect rect,
        my::TextureDownloadFormat format) override
    {
        return {};
    }

    void color_texture_download_async(
        uint64_t download_id,
        my::ResourceHandle framebuffer,
        my::Attachment color_attachment,
        my::Rect rect,
        my::TextureDownloadFormat format,
        my::ResourceHandle buffer) override
    {
    }

    void begin_time_query(uint64_t query_id, const char* name) override {}

    void end_time_query(uint64_t query_id) override {}
};

class MyPassCreate : public my::RenderPass
{
public:
    int* order_counter;
    int order;
    const char* res;
    my::ResourceHandle handle;

    MyPassCreate(int* order_counter, const char* res) :
        order_counter(order_counter), order(-1), res(res)
    {
    }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.create(
            res, my::RenderGraph::Target,
            my::RenderGraph::ResourceInfo{
                my::TextureFormat::RGB8,
                my::RenderGraph::ResourceInfo::BackbufferRelative,
                1.0f,
                1.0f,
            });
    }

    void retrieve_resources(
        my::Instance*,
        my::ResourceContext*,
        const my::RenderGraph::ResourceContext& my) override
    {
        handle = my.retrieve(res);
    }

    void execute(const my::RenderGraph::ExecutionContext&) override
    {
        order = *order_counter;
        *order_counter += 1;
        printf("create %s\n", res);
    }
};

class MyPassCreateMultiple : public my::RenderPass
{
public:
    int count;
    const char* res;

    MyPassCreateMultiple(int count_, const char* res_) : count(count_), res(res_) {}

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        for (int i = 0; i < count; ++i)
        {
            std::string s = std::string(res) + std::to_string(i);
            ctx.create(
                s.c_str(), my::RenderGraph::Target,
                my::RenderGraph::ResourceInfo{
                    my::TextureFormat::RGB8,
                    my::RenderGraph::ResourceInfo::BackbufferRelative,
                    1.0f,
                    1.0f,
                });
        }
    }

    void retrieve_resources(
        my::Instance*,
        my::ResourceContext*,
        const my::RenderGraph::ResourceContext& my) override
    {
    }

    void execute(const my::RenderGraph::ExecutionContext&) override { printf("create %s\n", res); }
};

class MyPassReadWriteSeparate : public my::RenderPass
{
public:
    int* order_counter;
    int order;
    const char* in;
    const char* out;
    my::ResourceHandle in_handle;
    my::ResourceHandle out_handle;

    MyPassReadWriteSeparate(int* order_counter, const char* in, const char* out) :
        order_counter(order_counter), order(-1), in(in), out(out)
    {
    }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read(in, my::RenderGraph::Target);
        ctx.create(
            out, my::RenderGraph::Target,
            my::RenderGraph::ResourceInfo{
                my::TextureFormat::RGB8,
                my::RenderGraph::ResourceInfo::BackbufferRelative,
                1.0f,
                1.0f,
            });
    }

    void retrieve_resources(
        my::Instance*,
        my::ResourceContext*,
        const my::RenderGraph::ResourceContext& my) override
    {
        in_handle = my.retrieve(in);
        out_handle = my.retrieve(out);
    }

    void execute(const my::RenderGraph::ExecutionContext&) override
    {
        order = *order_counter;
        *order_counter += 1;
        printf("read %s write %s\n", in, out);
    }
};

class MyPassReadWriteAndRead : public my::RenderPass
{
public:
    int* order_counter;
    int order;
    const char* in;
    const char* out;
    const char* read;
    my::ResourceHandle in_handle;
    my::ResourceHandle out_handle;
    my::ResourceHandle read_handle;

    MyPassReadWriteAndRead(int* order_counter, const char* in, const char* out, const char* read) :
        order_counter(order_counter), order(-1), in(in), out(out), read(read)
    {
    }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read_write(in, my::RenderGraph::Target, out);
        ctx.read(read, my::RenderGraph::Target);
    }

    void retrieve_resources(
        my::Instance*,
        my::ResourceContext*,
        const my::RenderGraph::ResourceContext& my) override
    {
        in_handle = my.retrieve(in);
        read_handle = my.retrieve(read);
        out_handle = my.retrieve(out);
    }

    void execute(const my::RenderGraph::ExecutionContext&) override
    {
        order = *order_counter;
        *order_counter += 1;
        printf("read %s and %s write %s\n", in, read, out);
    }
};

class MyPassReadWrite : public my::RenderPass
{
public:
    int* order_counter;
    int order;
    const char* in;
    const char* out;
    my::ResourceHandle in_handle;
    my::ResourceHandle out_handle;

    MyPassReadWrite(int* order_counter, const char* in, const char* out) :
        order_counter(order_counter), order(-1), in(in), out(out)
    {
    }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read_write(in, my::RenderGraph::Target, out);
    }

    void retrieve_resources(
        my::Instance*,
        my::ResourceContext*,
        const my::RenderGraph::ResourceContext& my) override
    {
        in_handle = my.retrieve(in);
        out_handle = my.retrieve(out);
    }

    void execute(const my::RenderGraph::ExecutionContext&) override
    {
        order = *order_counter;
        *order_counter += 1;
        printf("read %s write %s\n", in, out);
    }
};

class MyPassRead : public my::RenderPass
{
public:
    int* order_counter;
    int order;
    const char* name;
    my::ResourceHandle handle;

    MyPassRead(int* order_counter, const char* name) :
        order_counter(order_counter), order(-1), name(name)
    {
    }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read(name, my::RenderGraph::Target);
    }

    void retrieve_resources(
        my::Instance*,
        my::ResourceContext*,
        const my::RenderGraph::ResourceContext& my) override
    {
        handle = my.retrieve(name);
    }

    void execute(const my::RenderGraph::ExecutionContext&) override
    {
        order = *order_counter;
        *order_counter += 1;
        printf("read %s\n", name);
    }
};

class MyPassReadMultiple : public my::RenderPass
{
public:
    int count;
    const char* res;

    MyPassReadMultiple(int count_, const char* res_) : count(count_), res(res_) {}

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        for (int i = 0; i < count; ++i)
        {
            std::string s = std::string(res) + std::to_string(i);
            ctx.read(s.c_str(), my::RenderGraph::Target);
        }
    }

    void retrieve_resources(
        my::Instance*,
        my::ResourceContext*,
        const my::RenderGraph::ResourceContext& my) override
    {
    }

    void execute(const my::RenderGraph::ExecutionContext&) override { printf("read %s\n", res); }
};

TEST(RenderGraph, UnknownFinal)
{
    DummyInstance my;

    std::unique_ptr<my::RenderGraph> rg(my::RenderGraph::create());
    int order = 0;

    MyPassCreate pass(&order, "1");
    rg->add_pass("pass", &pass);

    my::RenderPassId id = 131354;
    ASSERT_FALSE(rg->build(&my, &my, 1, &id));
}

TEST(RenderGraph, SinglePass)
{
    DummyInstance my;

    std::unique_ptr<my::RenderGraph> rg(my::RenderGraph::create());
    int order = 0;

    MyPassCreate pass(&order, "1");
    auto final_pass = rg->add_pass("pass", &pass);

    ASSERT_TRUE(rg->build(&my, &my, 1, &final_pass));

    my::RenderGraph::ExecutionContext ctx = {};
    ctx.instance = &my;
    ctx.resource = &my;

    rg->execute(ctx);

    ASSERT_EQ(pass.order, 0);
}

TEST(RenderGraph, Disjoint)
{
    DummyInstance my;

    std::unique_ptr<my::RenderGraph> rg(my::RenderGraph::create());
    int order = 0;

    MyPassCreate pass1(&order, "1");
    auto final_pass = rg->add_pass("pass", &pass1);

    MyPassCreate pass2(&order, "3");
    rg->add_pass("pass2", &pass2);

    ASSERT_TRUE(rg->build(&my, &my, 1, &final_pass));

    my::RenderGraph::ExecutionContext ctx = {};
    ctx.instance = &my;
    ctx.resource = &my;

    rg->execute(ctx);

    EXPECT_EQ(pass1.order, 0);
    EXPECT_EQ(pass2.order, -1);
}

TEST(RenderGraph, ChainInOrder)
{
    DummyInstance my;

    std::unique_ptr<my::RenderGraph> rg(my::RenderGraph::create());
    int order = 0;

    MyPassCreate pass1(&order, "1");
    rg->add_pass("pass", &pass1);

    MyPassReadWriteSeparate pass2(&order, "1", "2");
    rg->add_pass("pass2", &pass2);

    MyPassReadWrite pass3(&order, "2", "3");
    auto final_pass = rg->add_pass("pass3", &pass3);

    ASSERT_TRUE(rg->build(&my, &my, 1, &final_pass));

    my::RenderGraph::ExecutionContext ctx = {};
    ctx.instance = &my;
    ctx.resource = &my;

    rg->execute(ctx);

    EXPECT_EQ(pass1.order, 0);
    EXPECT_EQ(pass2.order, 1);
    EXPECT_EQ(pass3.order, 2);
}

TEST(RenderGraph, ChainOutOfOrder)
{
    DummyInstance my;

    std::unique_ptr<my::RenderGraph> rg(my::RenderGraph::create());
    int order = 0;

    MyPassReadWrite pass3(&order, "2", "3");
    auto final_pass = rg->add_pass("pass3", &pass3);

    MyPassReadWriteSeparate pass2(&order, "1", "2");
    rg->add_pass("pass2", &pass2);

    MyPassCreate pass1(&order, "1");
    rg->add_pass("pass", &pass1);

    ASSERT_TRUE(rg->build(&my, &my, 1, &final_pass));

    my::RenderGraph::ExecutionContext ctx = {};
    ctx.instance = &my;
    ctx.resource = &my;

    rg->execute(ctx);

    EXPECT_EQ(pass1.order, 0);
    EXPECT_EQ(pass2.order, 1);
    EXPECT_EQ(pass3.order, 2);
}

TEST(RenderGraph, ReadWriteConflict)
{
    DummyInstance my;

    std::unique_ptr<my::RenderGraph> rg(my::RenderGraph::create());
    int order = 0;

    MyPassCreate pass1(&order, "1");
    rg->add_pass("pass", &pass1);

    MyPassReadWrite pass2(&order, "1", "2");
    auto final_pass = rg->add_pass("pass2", &pass2);

    MyPassRead pass3(&order, "1");
    rg->add_pass("pass3", &pass3);

    ASSERT_FALSE(rg->build(&my, &my, 1, &final_pass));
}

TEST(RenderGraph, Cycle)
{
    DummyInstance my;

    std::unique_ptr<my::RenderGraph> rg(my::RenderGraph::create());
    int order = 0;

    MyPassReadWrite pass1(&order, "2", "1");
    rg->add_pass("pass", &pass1);

    MyPassReadWrite pass2(&order, "1", "2");
    auto final_pass = rg->add_pass("pass2", &pass2);

    ASSERT_FALSE(rg->build(&my, &my, 1, &final_pass));
}

TEST(RenderGraph, ManyResources)
{
    DummyInstance my;

    std::unique_ptr<my::RenderGraph> rg(my::RenderGraph::create());

    MyPassCreateMultiple pass1(15, "resource");
    rg->add_pass("pass", &pass1);

    MyPassReadMultiple pass2(15, "resource");
    auto final_pass = rg->add_pass("pass2", &pass2);

    ASSERT_TRUE(rg->build(&my, &my, 1, &final_pass));
}

TEST(RenderGraph, CycleSeparate)
{
    DummyInstance my;

    std::unique_ptr<my::RenderGraph> rg(my::RenderGraph::create());
    int order = 0;

    MyPassReadWriteSeparate pass1(&order, "2", "1");
    rg->add_pass("pass", &pass1);

    MyPassReadWriteSeparate pass2(&order, "1", "2");
    auto final_pass = rg->add_pass("pass2", &pass2);

    ASSERT_FALSE(rg->build(&my, &my, 1, &final_pass));
}

TEST(RenderGraph, Complex)
{
    DummyInstance my;

    std::unique_ptr<my::RenderGraph> rg(my::RenderGraph::create());
    int order = 0;

    MyPassReadWriteAndRead pass_e(&order, "4", "5", "3");
    auto final_pass = rg->add_pass("E", &pass_e);

    MyPassCreate pass_c(&order, "3");
    rg->add_pass("C", &pass_c);

    MyPassReadWriteAndRead pass_d(&order, "2", "4", "1");
    rg->add_pass("D", &pass_d);

    MyPassReadWriteSeparate pass_a(&order, "3", "1");
    rg->add_pass("A", &pass_a);

    MyPassCreate pass_b(&order, "2");
    rg->add_pass("B", &pass_b);

    ASSERT_TRUE(rg->build(&my, &my, 1, &final_pass));

    my::RenderGraph::ExecutionContext ctx = {};
    ctx.instance = &my;
    ctx.resource = &my;

    rg->execute(ctx);

    EXPECT_TRUE(pass_a.order < pass_d.order);
    EXPECT_TRUE(pass_b.order < pass_d.order);
    EXPECT_TRUE(pass_c.order < pass_e.order && pass_c.order < pass_a.order);
    EXPECT_TRUE(pass_d.order < pass_e.order);
    EXPECT_TRUE(pass_e.order == 4);
}

TEST(RenderGraph, Subset)
{
    DummyInstance my;

    std::unique_ptr<my::RenderGraph> rg(my::RenderGraph::create());
    int order = 0;

    MyPassCreate pass_f(&order, "0");
    rg->add_pass("F", &pass_f);

    MyPassReadWriteAndRead pass_e(&order, "0", "5", "1");
    auto pass_e_id = rg->add_pass("E", &pass_e);

    MyPassCreate pass_c(&order, "3");
    rg->add_pass("C", &pass_c);

    MyPassReadWriteAndRead pass_d(&order, "2", "4", "1");
    auto pass_d_id = rg->add_pass("D", &pass_d);

    MyPassReadWriteSeparate pass_a(&order, "3", "1");
    rg->add_pass("A", &pass_a);

    MyPassCreate pass_b(&order, "2");
    rg->add_pass("B", &pass_b);

    my::RenderPassId final_passes[2] = {pass_e_id, pass_d_id};

    ASSERT_TRUE(rg->build(&my, &my, 2, final_passes));

    my::RenderPassId subset_final_passes[1] = {pass_d_id};

    auto subset_id = rg->make_subset(&my, 1, subset_final_passes);
    ASSERT_TRUE(subset_id != my::RenderGraph::SubsetError);

    my::RenderGraph::ExecutionContext ctx = {};
    ctx.instance = &my;
    ctx.resource = &my;

    rg->execute_subset(ctx, subset_id);

    EXPECT_TRUE(pass_a.order < pass_d.order);
    EXPECT_TRUE(pass_b.order < pass_d.order);
    EXPECT_TRUE(pass_c.order < pass_a.order);
    EXPECT_TRUE(pass_d.order == 3);
    EXPECT_TRUE(pass_e.order == -1);
}

TEST(RenderGraph, InvalidSubset)
{
    DummyInstance my;

    std::unique_ptr<my::RenderGraph> rg(my::RenderGraph::create());
    int order = 0;

    MyPassCreate pass_c(&order, "3");
    rg->add_pass("C", &pass_c);

    MyPassReadWriteAndRead pass_d(&order, "2", "4", "1");
    auto pass_d_id = rg->add_pass("D", &pass_d);

    MyPassReadWriteSeparate pass_a(&order, "3", "1");
    rg->add_pass("A", &pass_a);

    MyPassCreate pass_b(&order, "2");
    rg->add_pass("B", &pass_b);

    my::RenderPassId final_passes[1] = {pass_d_id};

    ASSERT_TRUE(rg->build(&my, &my, 1, final_passes));

    my::RenderPassId subset_final_passes[1] = {1000};

    auto subset_id = rg->make_subset(&my, 1, subset_final_passes);
    ASSERT_TRUE(subset_id == my::RenderGraph::SubsetError);
}

TEST(RenderGraph, ResourceAlias)
{
    DummyInstance my;

    std::unique_ptr<my::RenderGraph> rg(my::RenderGraph::create());
    int order = 0;

    MyPassCreate pass1(&order, "1");
    rg->add_pass("pass", &pass1);

    MyPassReadWriteSeparate pass2(&order, "1", "2");
    rg->add_pass("pass2", &pass2);

    MyPassReadWrite pass3(&order, "2", "3");
    rg->add_pass("pass3", &pass3);

    MyPassReadWriteSeparate pass4(&order, "3", "4");
    rg->add_pass("pass2", &pass4);

    MyPassReadWriteSeparate pass5(&order, "4", "5");
    auto final_pass = rg->add_pass("pass2", &pass5);

    ASSERT_TRUE(rg->build(&my, &my, 1, &final_pass));

    // Test continuity
    EXPECT_EQ(pass1.handle, pass2.in_handle);
    EXPECT_EQ(pass2.out_handle, pass3.in_handle);
    EXPECT_EQ(pass3.out_handle, pass4.in_handle);
    EXPECT_EQ(pass4.out_handle, pass5.in_handle);

    // Test logical aliasing
    EXPECT_EQ(pass3.in_handle, pass3.out_handle);

    // Test physical aliasing
    EXPECT_EQ(pass1.handle, pass4.out_handle);
    EXPECT_EQ(pass2.out_handle, pass5.out_handle);
}
