#pragma once

#include "hrz/core/model/shared_resources.h"
#include "hrz/core/model/ubo_defs.h"
#include "hrz/core/render.h"
#include "hrz/fnd/mem.h"

#include <lin_maths.h>
#include <mycelium/renderer.h>

namespace hrz::model
{
struct RenderablePrimitive : public my::Renderer::UserDataRenderable
{
    static constexpr size_t MaxShaderCollections = 2;

    struct ShaderCollection
    {
        my::ResourceHandle visual_opaque;
        my::ResourceHandle visual_transparent;
        my::ResourceHandle depth;
        my::ResourceHandle picking;
        my::ResourceHandle selection;
    };

    struct MeshRenderData
    {
        uint32_t scene_views_bitset{};
        bool render_selection{};
        bool cast_shadows{};
        bool is_transparent{};

        // Nothing is owned
        my::ResourceHandle ubo;
        // This is used to select variants of a shader based on primitive properties.
        // For example for batched models, variant 0 has integer batch ids, and variant 1 has
        // float batch ids. We place both collections here and the primitive can then
        // choose which one to use. This deduplicates shaders being put in primitive
        // render data, which can be in a much greater number that mesh render data.
        ShaderCollection shaders_collections[MaxShaderCollections];

        // Only filled when written to the renderer queue.
        // This points to memory in the renderer queue.
        std::span<const my::UboBinding> ubo_bindings;
        std::span<const my::TextureBinding> texture_bindings;
    };

    struct PrimitiveRenderData
    {
        const MeshRenderData* mesh; // Only filled when written to the arena. This points to memory
                                    // in the renderer queue.

        my::DrawBatchInfo batch;
        my::ResourceHandle vertex_input;
        my::ResourceHandle textures[MaterialCount];
        my::ResourceHandle samplers[MaterialCount];
        int shaders_collection; // Index in MeshRenderData.shaders_collections

        uint32_t prim_draw_ubo_offset;
        uint32_t prim_transform_ubo_offset;

        bool is_transparent;
    };

    PrimitiveRenderData primitive_data;
    hrz::BSphere<double> bsphere;

    static void render_callback(
        my::ResourceHandle shader,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const PrimitiveRenderData* data)
    {
        rb->push_state();

        my::UboBinding ubo_bindings[] = {
            {UboMeshParams, data->mesh->ubo, 0, sizeof(MeshUniformData)},
            {UboPrimitiveDrawParams, data->mesh->ubo, data->prim_draw_ubo_offset,
             sizeof(PrimitiveDrawUniformData)},
            {UboPrimitiveTransformParams, data->mesh->ubo, data->prim_transform_ubo_offset,
             sizeof(PrimitiveTransformUniformData)},
        };
        rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

        if (!data->mesh->ubo_bindings.empty())
        {
            rb->bind((uint32_t)data->mesh->ubo_bindings.size(), data->mesh->ubo_bindings.data());
        }

        my::TextureBinding texture_bindings[MaterialCount];
        for (int i = 0; i < MaterialCount; ++i)
        {
            texture_bindings[i] =
                my::TextureBinding{Material0Sampler + i, data->textures[i], data->samplers[i]};
        }
        rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

        if (!data->mesh->texture_bindings.empty())
        {
            rb->bind(
                (uint32_t)data->mesh->texture_bindings.size(), data->mesh->texture_bindings.data());
        }

        auto state = rb->get_current_state();

        r->draw(
            data->batch, shader, data->vertex_input, state.ubo_count, state.ubos,
            state.texture_count, state.textures);

        rb->pop_state();
    }

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* user_data_raw,
        const void* raw_data)
    {
        auto data = (const PrimitiveRenderData*)raw_data;

        auto user_data = (const SceneViewRenderGraphUserData*)user_data_raw;
        if (((1 << user_data->scene_view) & data->mesh->scene_views_bitset) == 0) return;

        const ShaderCollection& shaders = data->mesh->shaders_collections[data->shaders_collection];

        my::ResourceHandle shader;
        switch (render_type)
        {
            case RenderVisual:
                shader = (data->is_transparent || data->mesh->is_transparent)
                    ? shaders.visual_transparent
                    : shaders.visual_opaque;
                break;
            case RenderPicking: shader = shaders.picking; break;
            case RenderShadows:
            {
                if (!data->mesh->cast_shadows) return;
                shader = shaders.depth;
                break;
            }
            case RenderViewshed: shader = shaders.depth; break;
            case RenderSelection:
            {
                if (!data->mesh->render_selection) return;
                shader = shaders.selection;
                break;
            }
            default: return;
        }

        render_callback(shader, r, rb, data);
    }

    void collect_render_info_user_data(
        my::Renderer::Queue& queue,
        const my::Renderer::Culler& culler,
        void* user_data) const override
    {
        const auto* mesh_data = (const MeshRenderData*)user_data;
        auto bin_mask = (mesh_data->is_transparent || primitive_data.is_transparent)
            ? RenderWorldTransparentBin
            : RenderWorldOpaqueBin;

        if (culler.is_visible_in_any_view(bsphere.center, bsphere.radius, bin_mask))
        {
            PrimitiveRenderData new_render_data = primitive_data;
            new_render_data.mesh = mesh_data;
            queue.enqueue(
                bin_mask, render_callback, &new_render_data, bsphere.center, bsphere.radius);
        }
    }
};

} // namespace hrz::model
