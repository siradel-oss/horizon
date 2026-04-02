#pragma once

#include "hrz/core/model/model.h"
#include "hrz/protocol/3d_model/impostor_params.pb.h"

#include <cstdint>
#include <optional>

namespace hrz
{

struct ImpostorBaker;
struct Render;

namespace impostor
{

using BakingTicket = uint64_t;

enum class BakingStatus
{
    LoadingModel,
    Baking,
    Ready,
    Error,
};

struct BakedResources
{
    my::ResourceHandle color_texture = my::ResourceHandle::null();
    my::ResourceHandle normal_texture = my::ResourceHandle::null();
    my::ResourceHandle scale_coefficients_texture = my::ResourceHandle::null();
    double scale_correction;
    hrz::BSphere<double> model_bsphere;
};

/**
 * Create an impostor baking system.
 */
ImpostorBaker* create_system();

/**
 * Destroy the impostor baker system.
 */
void destroy_system(ImpostorBaker*, Render*);

/**
 * Start the baking of an impostor. A single model instance will be created from the model prototype
 * given in `advance_baking()`. The displayable model is then used to bake the impostor textures.
 */
BakingTicket bake_impostor(
    ImpostorBaker*,
    model::ModelPrototype*,
    const hrz_proto::Material&,
    const hrz_proto::ImpostorParams&,
    const model::DrawProperties&,
    const lm::dmat4& frame,
    uint64_t owner_layer_id);

/**
 * Query the state of the baking process.
 */
BakingStatus baking_status(ImpostorBaker*, BakingTicket);

/**
 * Decrease the reference count of an impostor and remove any data tied to
 * it if the reference count reaches zero.
 */
void destroy_impostor(ImpostorBaker*, model::ModelPrototype*, BakingTicket);

/**
 * Advance impostor baking processes.
 * Bake frames into the impostor texture.
 * Systems that use impostors are responsible for advancing their baking process.
 */
void advance_baking(
    ImpostorBaker*,
    Render*,
    model::ModelPrototype*,
    model::SharedResources*,
    BakingTicket);

void work_gpu(ImpostorBaker*, Render*);

/**
 * Retrieve handles to baked resources. Handles are still owned by the impostor system, thus they
 * *MUST NOT* be destroyed by the caller. To properly destroy every resources tied to an impostor
 * call `destroy_impostor()`.
 */
std::optional<BakedResources> get_baked_resources(ImpostorBaker*, BakingTicket);

} // namespace impostor
} // namespace hrz
