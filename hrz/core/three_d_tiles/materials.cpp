// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/model/model.h"
#include "hrz/core/three_d_tiles/system.h"
#include "hrz/core/three_d_tiles/tile_types.h"

#include <cassert>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hrz::three_d_tiles
{

namespace
{

struct B3dmOnlyVisitor
{
    void operator ()(Tileset&, ThreeDTile&, ThreeDTile::I3dmContent&) const
    {
        // No-op
    }

    void operator ()(Tileset&, ThreeDTile&, ThreeDTile::PntsContent&) const
    {
        // No-op
    }
};

struct RecreateAllMaterialsVisitor : public B3dmOnlyVisitor
{
    using B3dmOnlyVisitor::operator ();

    void operator ()(Tileset& tileset, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
    {
        b3d_model.recreate_all_materials(tileset.config->materials);
    }
};

struct AddMaterialVisitor : public B3dmOnlyVisitor
{
    using B3dmOnlyVisitor::operator ();

    const hrz_proto::Material& material;

    explicit AddMaterialVisitor(const hrz_proto::Material& mat) : material(mat) {}

    void operator ()(Tileset&, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
    {
        b3d_model.add_material(material);
    }
};

struct RemoveMaterialVisitor : public B3dmOnlyVisitor
{
    using B3dmOnlyVisitor::operator ();

    size_t index;

    explicit RemoveMaterialVisitor(size_t idx) : index(idx) {}

    void operator ()(Tileset&, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
    {
        b3d_model.remove_material(index);
    }
};

struct UpdateMaterialVisitor : public B3dmOnlyVisitor
{
    using B3dmOnlyVisitor::operator ();

    size_t index;
    const hrz_proto::Material& material;

    UpdateMaterialVisitor(size_t idx, const hrz_proto::Material& mat) : index(idx), material(mat) {}

    void operator ()(Tileset&, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
    {
        b3d_model.update_material(index, material);
    }
};

struct UpdateMaterialPaletteVisitor : public B3dmOnlyVisitor
{
    using B3dmOnlyVisitor::operator ();

    size_t index;
    const hrz_proto::NumericPalette& palette;

    UpdateMaterialPaletteVisitor(size_t idx, const hrz_proto::NumericPalette& pal) :
        index(idx), palette(pal)
    {
    }

    void operator ()(Tileset&, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
    {
        b3d_model.update_material_palette(index, palette);
    }
};

struct UpdateActiveMaterialsVisitor : public B3dmOnlyVisitor
{
    using B3dmOnlyVisitor::operator ();

    void operator ()(Tileset& tileset, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
    {
        b3d_model.update_active_materials(*tileset.config);
    }
};

struct UpdateMaterialPropertiesVisitor
{
    void operator ()(Tileset& tileset, ThreeDTile&, ContentSubtile auto& content) const
    {
        content.update_material_properties(tileset.config->inherited_draw_prps);
    }
};

struct UpdateAppearanceVisitor
{
    void operator ()(Tileset& tileset, ThreeDTile&, ContentSubtile auto& content) const
    {
        content.update_appearance(tileset.config->inherited_draw_prps);
    }
};

} // anonymous namespace

void ThreeDTilesSystem::recreate_all_materials(
    TilesetH handle,
    std::span<const hrz_proto::Material* const> materials)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    auto* config = tileset->get_mutable_config();

    config->materials.resize(materials.size());
    for (size_t i = 0; i < materials.size(); ++i)
    {
        config->materials[i] = *materials[i];
    }

    visit_tiles(handle, RecreateAllMaterialsVisitor{});
}

void ThreeDTilesSystem::add_material(TilesetH handle, const hrz_proto::Material& material)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    auto* config = tileset->get_mutable_config();

    config->materials.push_back(material);
    visit_tiles(handle, AddMaterialVisitor{material});
}

void ThreeDTilesSystem::remove_material(TilesetH handle, size_t index)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    auto* config = tileset->get_mutable_config();

    assert(index < config->materials.size());
    config->materials.erase(config->materials.begin() + index);

    visit_tiles(handle, RemoveMaterialVisitor{index});
}

void ThreeDTilesSystem::update_material(
    TilesetH handle,
    size_t index,
    const hrz_proto::Material& material)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    auto* config = tileset->get_mutable_config();

    assert(index < config->materials.size());
    config->materials[index] = material;

    visit_tiles(handle, UpdateMaterialVisitor{index, material});
}

void ThreeDTilesSystem::update_material_palette(
    TilesetH handle,
    size_t index,
    const hrz_proto::NumericPalette& palette)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    auto* config = tileset->get_mutable_config();

    assert(index < config->materials.size());
    *config->materials[index].mutable_data_texture_palette() = palette;

    visit_tiles(handle, UpdateMaterialPaletteVisitor{index, palette});
}

void ThreeDTilesSystem::update_material_properties(
    TilesetH handle,
    const hrz_proto::MaterialProperties& prps)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    auto* config = tileset->get_mutable_config();

    config->inherited_draw_prps.apply_feature_color_to_overlay =
        prps.apply_feature_color_to_overlay();
    config->inherited_draw_prps.overlay_material_enabled = prps.enable_overlay();
    config->inherited_draw_prps.overlay_material_opacity = prps.overlay_opacity();
    config->inherited_draw_prps.feature_color_blend_mode = prps.feature_color_blend_mode();
    config->inherited_draw_prps.feature_color_blend_strength = prps.feature_color_blend_strength();

    visit_tiles(handle, UpdateMaterialPropertiesVisitor{});
}

void ThreeDTilesSystem::update_active_materials(
    TilesetH handle,
    std::string_view base,
    std::optional<std::string_view> overlay)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    auto* config = tileset->get_mutable_config();

    config->base_material = std::string(base);
    if (overlay) config->overlay_material = std::string(overlay.value());

    visit_tiles(handle, UpdateActiveMaterialsVisitor{});
}

void ThreeDTilesSystem::update_appearance(
    TilesetH handle,
    int8_t clip_id,
    const hrz::render::LightingSettings& lighting_settings,
    bool draw_under_flat_overlays)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    auto* config = tileset->get_mutable_config();

    config->inherited_draw_prps.clip_id = clip_id;
    config->inherited_draw_prps.lighting = lighting_settings;
    config->inherited_draw_prps.draw_under_flat_overlays = draw_under_flat_overlays;

    visit_tiles(handle, UpdateAppearanceVisitor{});
}

} // namespace hrz::three_d_tiles
