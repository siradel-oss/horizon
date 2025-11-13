#include "hrz/core/impostor_baker.h"

#include "hrz/common/maths.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/core/render.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/mem.h"

namespace
{
size_t hash_impostor_identity(
    const hrz::model::ModelPrototype* model,
    const hrz_proto::Material& material,
    const hrz_proto::ImpostorParams& params)
{
    return hrz::hash_values(
        (uintptr_t)model, params.atlas_size().x(), params.atlas_size().y(),
        hrz::murmur3_x64_64(material.name().c_str()));
}
} // anonymous namespace

namespace
{
struct ImpostorBakingTechnique
{
    my::ResourceHandle color_texture = my::ResourceHandle::null();
    my::ResourceHandle normal_texture = my::ResourceHandle::null();
    my::ResourceHandle depth_texture = my::ResourceHandle::null();
    my::ResourceHandle fbo = my::ResourceHandle::null();

    lm::uvec4 viewport;
    hrz::model::DrawProperties draw_properties;

    lm::mat4 proj;
    lm::mat4 view;

    void initialize_rendering(
        hrz::Render* render,
        const hrz_proto::ImpostorParams& params,
        const hrz::model::DrawProperties& draw_prps,
        uint64_t owner_layer_id)
    {
        assert(render);

        const lm::uvec2 texture_size = {
            params.image_size().x() * params.atlas_size().x(),
            params.image_size().y() * params.atlas_size().y(),
        };

        {
            my::TextureResource res;
            res.layout.type = my::TextureLayout::Type2D;
            res.layout.format = my::TextureFormat::SRGBA8;
            res.layout.width = texture_size.x;
            res.layout.height = texture_size.y;
            res.layout.depth = 1;
            res.layout.levels = 1;
            res.data = {};
            res.generate_mipmaps = false;

            color_texture = render->rc->alloc(
                &res, hrz::monitoring::systems::Impostors, owner_layer_id,
                {{"contents"_ss, "color texture"_ss}});
        }

        {
            my::TextureResource res;
            res.layout.type = my::TextureLayout::Type2D;
            res.layout.format = my::TextureFormat::RG8;
            res.layout.width = texture_size.x;
            res.layout.height = texture_size.y;
            res.layout.depth = 1;
            res.layout.levels = 1;
            res.data = {};
            res.generate_mipmaps = false;

            normal_texture = render->rc->alloc(
                &res, hrz::monitoring::systems::Impostors, owner_layer_id,
                {{"contents"_ss, "normal texture"_ss}});
        }

        {
            my::TextureResource res;
            res.layout.type = my::TextureLayout::Type2D;
            res.layout.format = my::TextureFormat::Depth16;
            res.layout.width = texture_size.x;
            res.layout.height = texture_size.y;
            res.layout.depth = 1;
            res.layout.levels = 1;
            res.data = {};
            res.generate_mipmaps = false;

            depth_texture = render->rc->alloc(
                &res, hrz::monitoring::systems::Impostors, owner_layer_id,
                {{"contents"_ss, "depth texture"_ss}});
        }

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Depth, depth_texture},
                {my::Attachment::Color0, color_texture},
                {my::Attachment::Color1, normal_texture},
            };

            my::FramebufferResource res;
            res.attachment_count = HRZ_ARRAY_COUNT(attachments);
            res.attachments = attachments;

            fbo = render->rc->alloc(&res, hrz::monitoring::systems::Impostors);
        }

        draw_properties = draw_prps;
    }

    void execute(
        hrz::model::ModelPrototype* model_prototype,
        hrz::model::ImpostorBakingBakedModelH model,
        hrz::model::SharedResources* sr,
        hrz::Render* render)
    {
        const my::ClearTarget clear_values[] = {
            {my::Attachment::Color0, my::ClearValue::make_color_float(0, 0, 0, 0)},
            {my::Attachment::Color1, my::ClearValue::make_color_float(0, 0, 0, 0)},
            {my::Attachment::Depth, my::ClearValue::make_depth(1.0)},
        };

        const my::ViewportState viewport_state = {
            {viewport.x, viewport.y, viewport.z, viewport.w}, // viewport
            {viewport.x, viewport.y, viewport.z, viewport.w}, // scissor
        };

        render->my->set_framebuffer(fbo, viewport_state);
        render->my->clear(HRZ_ARRAY_COUNT(clear_values), clear_values);

        // We don't give an attribution registry because we don't want this render to count in the
        // attributions! Yes we're at risk of a segfault but that's OK, this will help us catch
        // places where we want the attributions registry to be optional.
        hrz::model::draw(model_prototype, model, draw_properties, proj, view, sr, render, nullptr);
    }

    void destroy(my::ResourceContext* rc)
    {
        rc->dealloc(depth_texture);
        rc->dealloc(fbo);
    }

    void set_matrices(const lm::mat4& proj, const lm::mat4& view)
    {
        this->proj = proj;
        this->view = view;
    }

    void set_viewport(const lm::uvec4& new_viewport) { viewport = new_viewport; }
};
} // anonymous namespace

namespace hrz
{
struct BakingProcess
{
    // Determines impostors equality (see `hash_impostor_identity()`).
    size_t identity;

    impostor::BakingStatus status;
    hrz_proto::ImpostorParams params;
    model::DrawProperties draw_prps;
    uint64_t owner_layer_id;

    hrz::BSphere<double> model_bsphere;
    lm::mat4 frame;

    lm::uvec2 next_atlas_position;
    impostor::BakedResources baked_resources;
    std::optional<ImpostorBakingTechnique> baking_technique = std::nullopt;

    // @Note: This is use to ensure that the prototype used to create the baking process is the same
    // as the model prototype used in `advance_baking()`. We rely on prototype which is
    // heap-allocated so its address is stable. A simple pointer comparison is performed for
    // equality.
    const void* model_prototype = nullptr;
    model::ImpostorBakingModelGeometryH model_geometry;
    model::ModelMaterialH model_material;
    model::ImpostorBakingBakedModelH baked_model;

    // Coefficients between 0 and 1 indicating, for each impostor frame, the contribution of the
    // scale along each axis of the 3D model to the resulting scale that should be applied to the
    // two axes of the quad used to render the image.
    // Each frame has its coefficients stored in two vec3s: the first vec3 holds the coefficients
    // for the quad's X axis, and the second vec3 holds the ones for the quad's Y axis.
    std::vector<lm::vec3> scale_coefficients;

    uint32_t ref_count;
};

struct ImpostorBaker
{
    using BakingProcessIndexPool = hrz::GenIndexPool<impostor::BakingTicket, 32, 32>;
    using BakingProcessPool = hrz::GenObjectPool<BakingProcess, BakingProcessIndexPool, 64>;

    BakingProcessPool process_pool;

    hrz::flat_hash_map<size_t, impostor::BakingTicket> identities_to_tickets;
    std::vector<impostor::BakingTicket> to_delete;
    std::vector<impostor::BakingTicket> baking_cleanup;
};

namespace impostor
{
void _work_cleanup(ImpostorBaker* ib, Render* render)
{
    for (BakingTicket ticket : ib->baking_cleanup)
    {
        BakingProcess* process = ib->process_pool.get_object(ticket);
        if (!process || !process->baking_technique.has_value())
        {
            continue;
        }

        process->baking_technique->destroy(render->my);
        process->baking_technique = std::nullopt;
    }
    ib->baking_cleanup.clear();

    for (BakingTicket ticket : ib->to_delete)
    {
        BakingProcess* process = ib->process_pool.get_object(ticket);

        render->rc->dealloc(process->baked_resources.color_texture);
        render->rc->dealloc(process->baked_resources.normal_texture);
        ib->identities_to_tickets.erase(process->identity);
        ib->process_pool.release(ticket);
    }
    ib->to_delete.clear();
}

ImpostorBaker* create_system()
{
    return new ImpostorBaker();
}

void destroy_system(ImpostorBaker* ib, Render* render)
{
    _work_cleanup(ib, render);
    delete ib;
}

BakingTicket bake_impostor(
    ImpostorBaker* ib,
    model::ModelPrototype* model_prototype,
    const hrz_proto::Material& material,
    const hrz_proto::ImpostorParams& params,
    const model::DrawProperties& draw_prps,
    const lm::dmat4& frame,
    uint64_t owner_layer_id)
{
    assert(ib);

    size_t identity = hash_impostor_identity(model_prototype, material, params);

    auto it = ib->identities_to_tickets.find(identity);
    if (it != ib->identities_to_tickets.end())
    {
        BakingProcess* process = ib->process_pool.get_object(it->second);
        assert(process);

        // We solely rely on the identity to compare impostors. As there aren't many impostors
        // created in a scene there shouldn't be collisions hopefully.
        if (process->identity == identity && process->ref_count > 0)
        {
            process->ref_count++;
            return it->second;
        }
    }

    BakingProcess process;
    process.status = BakingStatus::LoadingModel;
    process.identity = identity;
    process.model_prototype = model_prototype;
    process.draw_prps = draw_prps;
    process.params = params;
    process.owner_layer_id = owner_layer_id;
    process.ref_count = 1;

    process.model_geometry = model::create_impostor_baking_model_geometry(model_prototype);
    process.model_material = model::create_model_material(model_prototype, material);
    process.baked_model =
        model::create_baked_model(model_prototype, process.model_geometry, process.model_material);

    process.scale_coefficients.resize(
        process.params.atlas_size().x() * process.params.atlas_size().y() * 2);
    process.frame = (lm::mat4)frame;

    BakingTicket ticket = ib->process_pool.alloc(std::move(process));
    ib->identities_to_tickets.insert({identity, ticket});
    return ticket;
}

BakingStatus baking_status(ImpostorBaker* ib, BakingTicket ticket)
{
    assert(ib);
    BakingProcess* process = ib->process_pool.get_object(ticket);
    return process ? process->status : BakingStatus::Error;
}

void destroy_impostor(
    ImpostorBaker* ib,
    model::ModelPrototype* model_prototype,
    BakingTicket ticket)
{
    assert(ib);

    BakingProcess* process = ib->process_pool.get_object(ticket);
    if (process && process->model_prototype == model_prototype)
    {
        process->ref_count--;
        if (process->ref_count == 0)
        {
            model::destroy(model_prototype, process->baked_model);
            model::destroy(model_prototype, process->model_geometry);
            model::destroy(model_prototype, process->model_material);
            ib->baking_cleanup.push_back(ticket);
            ib->to_delete.push_back(ticket);
        }
    }
}

void advance_baking(
    ImpostorBaker* ib,
    Render* render,
    model::ModelPrototype* model_prototype,
    model::SharedResources* sr,
    BakingTicket ticket)
{
    HRZ_SCOPED_SAMPLE("impostor advance baking");
    assert(ib && render && model_prototype && sr);

    BakingProcess* process = ib->process_pool.get_object(ticket);

    if (process->status == BakingStatus::Ready) return;

    if (model_prototype != process->model_prototype)
    {
        HRZ_LOG_WARNING("Models prototypes don't match.");
        return;
    }

    auto model_status = model::get_status(model_prototype);
    if (model_status != model::ModelPrototypeStatus::Ready)
    {
        if (model_status == model::ModelPrototypeStatus::Error)
        {
            process->status = BakingStatus::Error;
        }

        return;
    }

    if (process->status == BakingStatus::LoadingModel)
    {
        auto baked_status = model::get_status(model_prototype, process->baked_model);
        if (baked_status == model::BakedModelStatus::Error)
        {
            process->status = BakingStatus::Error;
        }
        else if (
            baked_status == model::BakedModelStatus::Ready
            || baked_status == model::BakedModelStatus::ReadyWithErrors)
        {
            process->model_bsphere = model::compute_model_bsphere(
                model_prototype, process->model_geometry, process->draw_prps.transform);
            process->status = BakingStatus::Baking;
        }
        else
        {
            model::work(model_prototype, process->baked_model);
            model::work_gpu(model_prototype, process->baked_model, sr, render);
        }
    }

    if (process->status == BakingStatus::Baking)
    {
        assert(
            model::get_status(model_prototype, process->baked_model)
            == model::BakedModelStatus::Ready);

        if (!process->baking_technique.has_value())
        {
            process->baking_technique = ImpostorBakingTechnique();
            process->baking_technique->initialize_rendering(
                render, process->params, process->draw_prps, process->owner_layer_id);
        }

        lm::mat4 proj = lm::orthographic_opengl(
            (float)-process->model_bsphere.radius, (float)process->model_bsphere.radius,
            (float)-process->model_bsphere.radius, (float)process->model_bsphere.radius,
            (float)-process->model_bsphere.radius * 2, (float)process->model_bsphere.radius * 2);

        // Normalize atlas cell coordinates to UVs in the [-1, 1] range.
        lm::vec2 atlas_size =
            lm::vec2(process->params.atlas_size().x(), process->params.atlas_size().y());
        lm::vec2 atlas_size_minus_one = {
            std::max(1.0f, atlas_size.x - 1), std::max(1.0f, atlas_size.y - 1)};
        lm::vec2 atlas_uv_frame_size = lm::vec2(1.0f) / atlas_size_minus_one;
        lm::vec2 uv =
            lm::vec2(process->next_atlas_position) * atlas_uv_frame_size * 2.0f - lm::vec2(1.0f);

        // Convert UV coordinates to the hemi-octahedron pyramid `|x| + |y| + |z| = 1`
        lm::vec3 pos = {(uv.x + uv.y) * 0.5f, (-uv.x + uv.y) * 0.5f, 0};
        pos.z = 1 - std::abs(pos.x) - std::abs(pos.y);

        // Puff-out 3D coordinates to a unit sphere and scale.
        lm::vec3 pov = lm::normalize(pos) * (float)process->model_bsphere.radius;
        pov += lm::vec3(process->model_bsphere.center);

        auto up = lm::vec3(0, 0, 1);
        if (std::abs(lm::dot(pos, up)) > 0.99999)
        {
            up = lm::vec3(0, 1, 0);
        }
        lm::mat4 view = lm::view(pov, lm::vec3(process->model_bsphere.center), up);

        lm::mat4 view_cc = view;
        view_cc.w.xyz = lm::vec3(0);

        size_t frame_index = process->next_atlas_position.x
            + process->next_atlas_position.y * process->params.atlas_size().x();
        lm::vec3* scale_coefficients_x = &process->scale_coefficients.at(frame_index * 2);
        lm::vec3* scale_coefficients_y = &process->scale_coefficients.at(frame_index * 2 + 1);

        for (size_t axis = 0; axis < 3; ++axis)
        {
            lm::vec4 p_model = lm::vec4(0.0, 0.0, 0.0, 1.0);
            p_model.m[axis] = 1.0;

            lm::vec4 p_clip = (proj * view_cc * p_model);

            scale_coefficients_x->m[axis] =
                abs(p_clip.x / p_clip.w * process->model_bsphere.radius);
            scale_coefficients_y->m[axis] =
                abs(p_clip.y / p_clip.w * process->model_bsphere.radius);
        }

        lm::uvec4 viewport = {
            process->next_atlas_position.x * process->params.image_size().x(),
            process->next_atlas_position.y * process->params.image_size().y(),
            process->params.image_size().x(),
            process->params.image_size().y(),
        };

        process->baking_technique.value().set_viewport(viewport);
        process->baking_technique.value().set_matrices(proj, view);

        // Advance atlas positioning.
        process->next_atlas_position.x += 1;
        if (process->next_atlas_position.x == process->params.atlas_size().x())
        {
            process->next_atlas_position.x = 0;
            process->next_atlas_position.y += 1;
        }

        process->baking_technique.value().execute(
            model_prototype, process->baked_model, sr, render);

        if (process->next_atlas_position.y == process->params.atlas_size().y())
        {
            process->baked_resources.color_texture =
                process->baking_technique.value().color_texture;
            process->baked_resources.normal_texture =
                process->baking_technique.value().normal_texture;
            process->baked_resources.scale_correction = 2 * process->model_bsphere.radius;
            process->baked_resources.model_bsphere = process->model_bsphere;

            {
                std::span<const std::byte> scale_coefficients_data(
                    (std::byte*)process->scale_coefficients.data(),
                    process->scale_coefficients.size() * sizeof(lm::vec3));

                my::TextureResource tex_res;
                tex_res.layout.type = my::TextureLayout::Type2D;
                tex_res.layout.format = my::TextureFormat::RGB32F;
                tex_res.layout.width =
                    process->params.atlas_size().x() * process->params.atlas_size().y() * 2;
                tex_res.layout.height = 1;
                tex_res.layout.depth = 1;
                tex_res.layout.levels = 1;
                tex_res.generate_mipmaps = false;
                tex_res.allow_allocation_failure = true;
                tex_res.data = {&scale_coefficients_data, 1};

                process->baked_resources.scale_coefficients_texture = render->rc->alloc(
                    &tex_res, hrz::monitoring::systems::Impostors, process->owner_layer_id,
                    {{"contents"_ss, "scale coefficients texture"_ss}});
            }

#if 0
            // Loop the impostor baking pass making it easy to be captured in RenderDoc
            // or similar tools.
            process->next_atlas_position.x = 0;
            process->next_atlas_position.y = 0;
#else
            process->status = BakingStatus::Ready;
            ib->baking_cleanup.push_back(ticket);
#endif
        }
    }
}

void work_gpu(ImpostorBaker* ib, Render* render)
{
    HRZ_SCOPED_SAMPLE("impostor work gpu");
    assert(ib && render);

    _work_cleanup(ib, render);
}

std::optional<BakedResources> get_baked_resources(ImpostorBaker* ib, BakingTicket ticket)
{
    assert(ib);
    BakingProcess* process = ib->process_pool.get_object(ticket);
    return (process && process->status == BakingStatus::Ready)
        ? std::optional<BakedResources>(process->baked_resources)
        : std::nullopt;
}
} // namespace impostor
} // namespace hrz
