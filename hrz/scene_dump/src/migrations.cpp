#include "dynamic_message.h"

#include <array>
#include <cmath>
#include <unordered_map>

namespace hrz::migration
{
namespace
{
bool walk_common_fields(
    const DynamicMessage& src_msg,
    DynamicMessage* dst_msg,
    std::function<bool(const DynamicMessage&, DynamicMessage*)> migration_func)
{
    if (!migration_func(src_msg, dst_msg)) return false;

    bool success = true;

    auto descriptor = src_msg.msg->GetDescriptor();
    for (int i = 0; i < descriptor->field_count(); ++i)
    {
        auto field = descriptor->field(i);
        auto field_name = field->name().c_str();

        if (dst_msg->msg->GetDescriptor()->FindFieldByName(field_name) == nullptr) continue;

        if (field->is_repeated())
        {
            if (src_msg.field_size(field_name) == 0) continue;
            if (dst_msg->field_size(field_name) == 0) continue;
        }
        else
        {
            if (!src_msg.has_field(field_name)) continue;
            if (!dst_msg->has_field(field_name)) continue;
        }

        if (field->type() == google::protobuf::FieldDescriptor::Type::TYPE_MESSAGE)
        {
            if (field->is_repeated())
            {
                for (int j = 0; j < src_msg.field_size(field_name); ++j)
                {
                    auto child_src_msg = src_msg.get_repeated_message(field_name, j);
                    auto child_dst_msg = dst_msg->get_repeated_message(field_name, j);
                    success &= walk_common_fields(child_src_msg, &child_dst_msg, migration_func);
                }
            }
            else
            {
                auto child_src_msg = src_msg.get_message(field_name);
                auto child_dst_msg = dst_msg->get_message(field_name);
                success &= walk_common_fields(child_src_msg, &child_dst_msg, migration_func);
            }
        }
    }

    return success;
}

bool visit_layers_of_type(
    const char* layer_type_name,
    const DynamicMessage& src,
    DynamicMessage* dst,
    std::function<bool(const DynamicMessage&, DynamicMessage*)> cb)
{
    int layer_count = src.field_size("layers");
    if (dst->field_size("layers") != layer_count) return false;

    for (int i = 0; i < layer_count; ++i)
    {
        auto src_layer_dump = src.get_repeated_message("layers", i);
        auto dst_layer_dump = dst->get_repeated_message("layers", i);

        if (src_layer_dump.get_oneof_field_name("kind") == layer_type_name
            && dst_layer_dump.get_oneof_field_name("kind") == layer_type_name)
        {
            auto src_layer = src_layer_dump.get_message(layer_type_name);
            auto dst_layer = dst_layer_dump.get_message(layer_type_name);
            if (!cb(src_layer, &dst_layer)) return false;
        }
    }

    return true;
}

bool walk_fields_of_type(
    const char* name,
    const DynamicMessage& src,
    DynamicMessage* dst,
    std::function<bool(const DynamicMessage&, DynamicMessage*)> cb)
{
    return walk_common_fields(
        src, dst,
        [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
        {
            if (dst->get_type_name() == name)
            {
                return cb(src, dst);
            }
            else
            {
                return true;
            }
        });
}
} // namespace

// Add support for multi-materials
bool migration_00000000_to_5d6d6cce(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        std::string active_material_name = src.get_string("active_material");

        DynamicMessage material_properties = dst->get_message("material_properties");
        material_properties.set_string("base_material", active_material_name);
        material_properties.set_float("overlay_opacity", 1.0f);
        material_properties.set_bool("enable_overlay", false);

        return true;
    };

    return visit_layers_of_type("single_model", src, dst, migrate_fn)
        && visit_layers_of_type("three_d_tiles", src, dst, migrate_fn);
}

// Indicate presence of optional fields with booleans
bool migration_5d6d6cce_to_22e6ae36(const DynamicMessage& src_msg, DynamicMessage* dst_msg)
{
    auto migration_func = [](const DynamicMessage& src_msg, DynamicMessage* dst_msg)
    {
        auto message_type_name = src_msg.msg->GetDescriptor()->full_name().c_str();

        if (strcmp(message_type_name, "HrzProtocol.RasterParams") == 0)
        {
            if (dst_msg->has_field("display_bounds"))
            {
                auto display_bounds = dst_msg->get_message("display_bounds");
                auto west = display_bounds.get_double("west");
                auto south = display_bounds.get_double("south");
                auto east = display_bounds.get_double("east");
                auto north = display_bounds.get_double("north");

                if (west == 0.0 && south == 0.0 && east == 0.0 && north == 0.0)
                {
                    display_bounds.set_double("west", -180.0);
                    display_bounds.set_double("south", -90.0);
                    display_bounds.set_double("east", 180.0);
                    display_bounds.set_double("north", 90.0);
                }
            }
            else
            {
                auto display_bounds = dst_msg->get_message("display_bounds");
                display_bounds.set_double("west", -180.0);
                display_bounds.set_double("south", -90.0);
                display_bounds.set_double("east", 180.0);
                display_bounds.set_double("north", 90.0);
            }
        }
        else if (strcmp(message_type_name, "HrzProtocol.VectorTilesSource") == 0)
        {
            dst_msg->set_bool("override_levels", true);
            dst_msg->set_bool("override_bounds", true);
        }

        return true;
    };

    return walk_common_fields(src_msg, dst_msg, migration_func);
}

// Add color blend mode and strength to single model layers, 3D tiles layers, and sprite
// and model representations
bool migration_22e6ae36_to_72f11ce6(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_models_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto prps = dst->get_message("material_properties");
        prps.set_enum("feature_color_blend_mode", "BLEND_MULTIPLY");
        prps.set_float("feature_color_blend_strength", 1.0);
        return true;
    };

    auto migrate_vector_tiles_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto style = dst->get_message("style");
        size_t repr_count = style.field_size("representations");
        for (size_t j = 0; j < repr_count; ++j)
        {
            auto repr = style.get_repeated_message("representations", j);
            if (repr.get_enum("type") == "SPRITE")
            {
                auto sprite = repr.get_message("sprite");
                sprite.set_enum("feature_color_blend_mode", "BLEND_MULTIPLY");
                sprite.set_float("feature_color_blend_strength", 1.0f);
            }
            else if (repr.get_enum("type") == "MODEL")
            {
                auto model = repr.get_message("model");
                model.set_enum("feature_color_blend_mode", "BLEND_MULTIPLY");
                model.set_float("feature_color_blend_strength", 1.0f);
            }
        }

        return true;
    };

    return visit_layers_of_type("single_model", src, dst, migrate_models_fn)
        && visit_layers_of_type("three_d_tiles", src, dst, migrate_models_fn)
        && visit_layers_of_type("vector_tiles", src, dst, migrate_vector_tiles_fn);
}

// Add support for gizmo sizes in meters and relative to the window
bool migration_72f11ce6_to_92e811b6(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migration_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        dst->set_enum("size_unit", "UI_SIZE_IN_PIXELS");
        return true;
    };

    return visit_layers_of_type("gizmo", src, dst, migration_fn);
}

bool migration_92e811b6_to_b0b39627(const DynamicMessage& src, DynamicMessage* dst)
{
    uint32_t highest_attribute_id = 0;

    auto find_attribute_ids_in_vector_data_layers = [&](const DynamicMessage& src,
                                                        DynamicMessage*) -> bool
    {
        for (int i = 0; i < src.field_size("sources"); ++i)
        {
            auto source = src.get_repeated_message("sources", i);
            for (int j = 0; j < source.field_size("attributes"); ++j)
            {
                auto attribute = source.get_repeated_message("attributes", j);
                highest_attribute_id = std::max(highest_attribute_id, attribute.get_uint32("id"));
            }
        }
        return true;
    };
    auto find_attribute_ids_in_in_memory_layers = [&](const DynamicMessage& src,
                                                      DynamicMessage*) -> bool
    {
        for (int j = 0; j < src.field_size("attributes"); ++j)
        {
            auto attribute = src.get_repeated_message("attributes", j);
            highest_attribute_id = std::max(highest_attribute_id, attribute.get_uint32("id"));
        }
        return true;
    };
    visit_layers_of_type("vector_data", src, dst, find_attribute_ids_in_vector_data_layers);
    visit_layers_of_type(
        "in_memory_vector_source", src, dst, find_attribute_ids_in_in_memory_layers);

    auto get_new_attribute_id = [&]()
    {
        highest_attribute_id += 1;
        return highest_attribute_id;
    };

    std::unordered_map<uint32_t, uint32_t> vector_data_layers_to_feature_id_attribute;
    std::unordered_map<uint32_t, uint32_t> in_memory_layers_to_feature_id_attribute;

    auto migrate_vector_data_layers = [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        bool has_feature_id_attribute = false;
        uint32_t feature_id_attribute_id = 0;

        for (int i = 0; i < src.field_size("sources"); ++i)
        {
            auto source = src.get_repeated_message("sources", i);
            if (source.get_bool("has_geometry") || source.get_bool("has_feature_ids"))
            {
                feature_id_attribute_id = get_new_attribute_id();
                has_feature_id_attribute = true;

                auto id_attribute = dst->create_message("HrzProtocol.VectorAttribute");
                id_attribute.set_uint32("id", feature_id_attribute_id);
                id_attribute.set_enum("type", "UINT_ATTRIBUTE");
                id_attribute.set_bool("is_feature_id", true);

                if (source.get_enum("format") == "GEOJSON_VECTOR_DATA"
                    || source.get_enum("format") == "MVT_VECTOR_DATA"
                    || source.get_enum("format") == "GEOBUF_VECTOR_DATA")
                {
                    auto id_field_name = source.get_string("id_field_name");
                    if (id_field_name != "")
                    {
                        id_attribute.set_string("source_name", id_field_name);
                    }
                    else
                    {
                        id_attribute.set_bool("is_source_feature_ids", true);
                    }
                }

                dst->get_repeated_message("sources", i)
                    .add_repeated_message("attributes", id_attribute);

                break;
            }
        }

        if (has_feature_id_attribute)
        {
            for (int i = 0; i < src.field_size("sources"); ++i)
            {
                auto source = src.get_repeated_message("sources", i);
                if (source.get_enum("format") == "IN_MEMORY_VECTOR_DATA")
                {
                    auto in_memory_layer = source.get_uint32("in_memory_layer_id");
                    in_memory_layers_to_feature_id_attribute[in_memory_layer] =
                        feature_id_attribute_id;
                }
            }
        }

        return true;
    };

    auto migrate_in_memory_layers = [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        {
            uint32_t feature_id_attribute_id = 0;

            auto in_memory_layer_id = src.get_uint32("id");
            {
                auto it = in_memory_layers_to_feature_id_attribute.find(in_memory_layer_id);
                if (it != in_memory_layers_to_feature_id_attribute.end())
                {
                    feature_id_attribute_id = it->second;
                }
                else
                {
                    feature_id_attribute_id = get_new_attribute_id();
                }
            }

            auto feature_id_attribute = dst->create_message("HrzProtocol.InMemoryAttribute");
            feature_id_attribute.set_uint32("id", feature_id_attribute_id);
            feature_id_attribute.set_enum("type", "UINT_ATTRIBUTE");
            feature_id_attribute.set_bool("is_feature_id", true);
            dst->add_repeated_message("attributes", feature_id_attribute);
        }

        for (int i = 0; i < src.field_size("features"); ++i)
        {
            auto src_feature = src.get_repeated_message("features", i);
            auto feature_id = src_feature.get_uint32("id");

            auto dst_feature = dst->create_message("HrzProtocol.InMemoryVectorFeature");
            auto dst_geometry = dst->create_message("HrzProtocol.InMemoryVectorFeatureGeometry");
            dst_geometry.set_enum("type", src_feature.get_enum("type").data());
            for (int j = 0; j < src_feature.field_size("coords"); ++j)
            {
                dst_geometry.add_double("coords", src_feature.get_repeated_double("coords", j));
            }
            for (int j = 0; j < src_feature.field_size("ring_sizes"); ++j)
            {
                dst_geometry.add_uint32(
                    "ring_sizes", src_feature.get_repeated_uint32("ring_sizes", j));
            }
            dst_feature.set_message("geometry", dst_geometry);

            for (int j = 0; j < src.field_size("attributes"); ++j)
            {
                auto src_attribute = src.get_repeated_message("attributes", j);

                auto dst_value = dst->create_message("HrzProtocol.InMemoryAttributeValue");

                bool found = false;
                for (int k = 0; k < src_attribute.field_size("values"); ++k)
                {
                    auto src_value = src_attribute.get_repeated_message("values", k);

                    if (src_value.get_uint32("feature_id") == feature_id)
                    {
                        dst_value.set_bool("boolean_value", src_value.get_bool("boolean_value"));
                        dst_value.set_int64("int_value", src_value.get_int64("int_value"));
                        dst_value.set_uint64("uint_value", src_value.get_uint64("uint_value"));
                        dst_value.set_double("float_value", src_value.get_double("float_value"));
                        dst_value.set_string("string_value", src_value.get_string("string_value"));
                        dst_value.set_string("color_value", src_value.get_string("color_value"));
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    dst_value.set_bool("boolean_value", false);
                    dst_value.set_int64("int_value", 0);
                    dst_value.set_uint64("uint_value", 0);
                    dst_value.set_double("float_value", 0.0);
                    dst_value.set_string("string_value", "");
                    dst_value.set_string("color_value", "#000000");
                }

                dst_feature.add_repeated_message("attribute_values", dst_value);
            }

            {
                auto dst_value = dst->create_message("HrzProtocol.InMemoryAttributeValue");
                dst_value.set_uint64("uint_value", feature_id);
                dst_feature.add_repeated_message("attribute_values", dst_value);
            }

            dst->add_repeated_message("features", dst_feature);
        }

        return true;
    };

    auto migrate_3d_tiles_layers = [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        for (int i = 0; i < src.field_size("attributes"); ++i)
        {
            auto src_attribute = src.get_repeated_message("attributes", i);
            auto dst_attribute = dst->get_repeated_message("attributes", i);

            if (src_attribute.get_bool("from_vector_data_layer"))
            {
                dst_attribute.set_enum("source", "VECTOR_DATA_LAYER_SOURCE");
                dst_attribute.set_bool("is_feature_id", false);
            }
        }

        auto id_attribute_name = src.get_string("id_attribute_name");
        if (id_attribute_name != "")
        {
            auto vector_data_layer_id = src.get_uint32("vector_data_layer_id");
            uint32_t feature_id_attribute_id = 0;

            {
                auto it = vector_data_layers_to_feature_id_attribute.find(vector_data_layer_id);
                if (it != vector_data_layers_to_feature_id_attribute.end())
                {
                    feature_id_attribute_id = it->second;
                }
                else
                {
                    feature_id_attribute_id = get_new_attribute_id();
                }
            }

            auto dst_id_attribute = dst->create_message("HrzProtocol.ThreeDTileAttribute");
            dst_id_attribute.set_string("name", id_attribute_name);
            dst_id_attribute.set_enum("type", "UINT_ATTRIBUTE");
            dst_id_attribute.set_enum("source", "BATCH_TABLE_SOURCE");
            dst_id_attribute.set_uint32("vector_data_attr_id", feature_id_attribute_id);
            dst_id_attribute.set_bool("is_feature_id", true);
            dst->add_repeated_message("attributes", dst_id_attribute);
        }

        auto batch_class_id_attribute_name = src.get_string("batch_class_id_attribute_name");
        if (batch_class_id_attribute_name != "")
        {
            auto dst_attribute = dst->create_message("HrzProtocol.ThreeDTileAttribute");
            dst_attribute.set_string("name", batch_class_id_attribute_name);
            dst_attribute.set_enum("type", "UINT_ATTRIBUTE");
            dst_attribute.set_enum("source", "BATCH_CLASS_ID_SOURCE");
            dst_attribute.set_bool("is_feature_id", false);
            dst->add_repeated_message("attributes", dst_attribute);
        }

        auto batch_class_name_attribute_name = src.get_string("batch_class_name_attribute_name");
        if (batch_class_id_attribute_name != "")
        {
            auto dst_attribute = dst->create_message("HrzProtocol.ThreeDTileAttribute");
            dst_attribute.set_string("name", batch_class_name_attribute_name);
            dst_attribute.set_enum("type", "STRING_ATTRIBUTE");
            dst_attribute.set_enum("source", "BATCH_CLASS_NAME_SOURCE");
            dst_attribute.set_bool("is_feature_id", false);
            dst->add_repeated_message("attributes", dst_attribute);
        }

        return true;
    };

    return visit_layers_of_type("vector_data", src, dst, migrate_vector_data_layers)
        && visit_layers_of_type("in_memory_vector_source", src, dst, migrate_in_memory_layers)
        && visit_layers_of_type("three_d_tiles", src, dst, migrate_3d_tiles_layers);
}

// Set 3D Tiles refinement hysteresis to a value approaching the previous behaviour.
bool migration_b0b39627_to_3e3b8b93(const DynamicMessage& src_msg, DynamicMessage* dst_msg)
{
    auto migration_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        dst->set_float("refinement_hysteresis", 0.3);
        return true;
    };

    return visit_layers_of_type("three_d_tiles", src_msg, dst_msg, migration_fn);
}

// Replace default dash ratio of polylines with default dash length, and set the according dash mode
bool migration_3e3b8b93_to_66c02430(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migration_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto src_style = src.get_message("style");
        auto dst_style = dst->get_message("style");

        size_t repr_count = dst_style.field_size("representations");
        for (size_t j = 0; j < repr_count; ++j)
        {
            auto src_repr = src_style.get_repeated_message("representations", j);
            auto dst_repr = dst_style.get_repeated_message("representations", j);

            if (dst_repr.get_enum("type") != "FLAT_OVERLAY") continue;

            auto src_flat_overlay = src_repr.get_message("flat_overlay_geometry");
            auto dst_flat_overlay = dst_repr.get_message("flat_overlay_geometry");

            float dash_ratio = src_flat_overlay.get_float("default_dash_ratio");
            if (dash_ratio < 1.0)
            {
                dst_flat_overlay.set_enum("dash_mode", "DASH_ENABLED_FILLED");

                float dash_length = dash_ratio * src_flat_overlay.get_float("default_dash_period");
                dst_flat_overlay.set_float("default_dash_length", dash_length);
            }
        }

        return true;
    };

    return visit_layers_of_type("vector_tiles", src, dst, migration_fn);
}

bool migration_66c02430_to_e677ffef(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migration_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        int source_count = src.field_size("sources");
        if (dst->field_size("sources") != source_count) return false;

        for (int i = 0; i < source_count; ++i)
        {
            auto src_source = src.get_repeated_message("sources", i);
            auto dst_source = dst->get_repeated_message("sources", i);

            auto format = src_source.get_enum("format");
            if (format == "GEOJSON_VECTOR_DATA" || format == "MVT_VECTOR_DATA"
                || format == "GEOBUF_VECTOR_DATA")
            {
                dst_source.set_enum("provider_type", "TILED_VECTOR_DATA_PROVIDER");

                auto dst_provider =
                    dst_source.create_message("HrzProtocol.TiledVectorDataProviderParams");
                dst_provider.set_string("url_pattern", src_source.get_string("url_pattern"));
                dst_provider.copy_message("http_headers", src_source.get_message("http_headers"));
                dst_provider.set_enum("format", format);
                dst_provider.set_string("layer_name", src_source.get_string("layer_name"));
                dst_source.set_message("tiled_data_provider", dst_provider);
            }
            else if (format == "CLIENT_VECTOR_DATA")
            {
                dst_source.set_enum("provider_type", "CLIENT_VECTOR_DATA_PROVIDER");

                auto dst_provider =
                    dst_source.create_message("HrzProtocol.ClientVectorDataProviderParams");
                dst_provider.set_enum("access", src_source.get_enum("access"));
                dst_source.set_message("client_data_provider", dst_provider);
            }
            else if (format == "IN_MEMORY_VECTOR_DATA")
            {
                dst_source.set_enum("provider_type", "IN_MEMORY_VECTOR_DATA_PROVIDER");

                auto dst_provider =
                    dst_source.create_message("HrzProtocol.InMemoryVectorDataProviderParams");
                dst_provider.set_uint32(
                    "in_memory_layer_id", src_source.get_uint32("in_memory_layer_id"));
                dst_source.set_message("in_memory_data_provider", dst_provider);
            }
            else
            {
                return false;
            }
        }

        return true;
    };

    return visit_layers_of_type("vector_data", src, dst, migration_fn);
}

// Adds the default camera inertia value
bool migration_e677ffef_to_ead9a411(const DynamicMessage& src, DynamicMessage* dst)
{
    int camera_count = dst->field_size("camera_settings");
    for (int i = 0; i < camera_count; ++i)
    {
        auto camera_settings =
            dst->get_repeated_message("camera_settings", i).get_message("settings");
        camera_settings.set_float("user_controls_inertia", 0.06);
        camera_settings.set_float("movements_inertia", 0.06);
    }

    return true;
}

// Replaces style scripts 'emit_id' and 'emit_named' instructions by the new 'emit' instruction.
bool migration_ead9a411_to_c3c66848(const DynamicMessage& src, DynamicMessage* dst)
{
    auto replace = [](std::string& str, const std::string& pattern, const std::string& substitute)
    {
        size_t pos = str.find(pattern);
        while (pos != std::string::npos)
        {
            str.replace(pos, pattern.size(), substitute);
            pos += substitute.size();
            pos = str.find(pattern, pos);
        }
    };

    auto migrate_styling_script = [&](std::string& script)
    {
        replace(script, "emit_id", "emit");
        replace(script, "emit_named", "emit");
    };

    auto migrate_3d_tiles_layers = [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto script = src.get_string("styling_script");
        migrate_styling_script(script);

        dst->set_string("styling_script", script);

        return true;
    };

    auto migrate_vector_tiles_layers = [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto src_style_msg = src.get_message("style");
        auto dst_style_msg = dst->get_message("style");

        auto script = src_style_msg.get_string("styling_script");
        migrate_styling_script(script);

        dst_style_msg.set_string("styling_script", script);

        return true;
    };

    return visit_layers_of_type("three_d_tiles", src, dst, migrate_3d_tiles_layers)
        && visit_layers_of_type("vector_tiles", src, dst, migrate_vector_tiles_layers);
}

// Runtime provided properties. Use the old property name in the new property name text field.
bool migration_c3c66848_to_7363df2a(const DynamicMessage& src, DynamicMessage* dst)
{
#define UPDATE_SCALAR_FIELD(NAME, TYPE)                                     \
    do                                                                      \
    {                                                                       \
        auto field = dst.get_message(NAME);                                 \
        field.set_string("name", NAME);                                     \
        field.set_##TYPE("default_value", src.get_##TYPE("default_" NAME)); \
    } while (0)

#define UPDATE_MESSAGE_FIELD(NAME)                                             \
    do                                                                         \
    {                                                                          \
        auto field = dst.get_message(NAME);                                    \
        field.set_string("name", NAME);                                        \
        field.copy_message("default_value", src.get_message("default_" NAME)); \
    } while (0)

    auto migrate_extruded_repr = [&](const DynamicMessage& src, DynamicMessage& dst) -> bool
    {
        UPDATE_SCALAR_FIELD("extrusion", float);
        UPDATE_MESSAGE_FIELD("color");
        UPDATE_SCALAR_FIELD("altitude_offset", float);
        return true;
    };

    auto migrate_sprite_repr = [&](const DynamicMessage& src, DynamicMessage& dst) -> bool
    {
        UPDATE_MESSAGE_FIELD("color");
        UPDATE_SCALAR_FIELD("size", float);
        UPDATE_MESSAGE_FIELD("world_offset");
        UPDATE_MESSAGE_FIELD("screen_offset");

        auto scale = dst.get_message("scale");
        scale.set_string("name", "scale");
        scale.set_float("default_value", 1.0);

        return true;
    };

    auto migrate_model_repr = [&](const DynamicMessage& src, DynamicMessage& dst) -> bool
    {
        UPDATE_MESSAGE_FIELD("color");
        UPDATE_SCALAR_FIELD("scale", float);
        UPDATE_MESSAGE_FIELD("world_offset");
        UPDATE_MESSAGE_FIELD("rotation");
        return true;
    };

    auto migrate_stem_repr = [&](const DynamicMessage& src, DynamicMessage& dst) -> bool
    {
        UPDATE_MESSAGE_FIELD("color");
        UPDATE_SCALAR_FIELD("height", float);
        UPDATE_SCALAR_FIELD("altitude_offset", float);
        return true;
    };

    auto migrate_cylinder_repr = [&](const DynamicMessage& src, DynamicMessage& dst) -> bool
    {
        UPDATE_MESSAGE_FIELD("color");
        UPDATE_SCALAR_FIELD("radius", float);
        UPDATE_SCALAR_FIELD("altitude_offset", float);
        UPDATE_SCALAR_FIELD("dash_period", float);
        UPDATE_SCALAR_FIELD("dash_length", float);
        UPDATE_MESSAGE_FIELD("empty_color");
        UPDATE_SCALAR_FIELD("animation_speed", float);
        return true;
    };

    auto migrate_text_repr = [&](const DynamicMessage& src, DynamicMessage& dst) -> bool
    {
        UPDATE_MESSAGE_FIELD("text_color");
        UPDATE_MESSAGE_FIELD("outline_color");
        UPDATE_MESSAGE_FIELD("background_color");
        UPDATE_SCALAR_FIELD("size", float);
        UPDATE_MESSAGE_FIELD("world_offset");
        UPDATE_MESSAGE_FIELD("screen_offset");

        auto text = dst.get_message("text");
        text.set_string("name", "text");
        text.set_string("default_value", "");

        auto scale = dst.get_message("scale");
        scale.set_string("name", "scale");
        scale.set_float("default_value", 1.0);

        return true;
    };

    auto migrate_flat_overlay_repr = [&](const DynamicMessage& src, DynamicMessage& dst) -> bool
    {
        UPDATE_SCALAR_FIELD("line_width", float);
        UPDATE_MESSAGE_FIELD("color");
        UPDATE_SCALAR_FIELD("disc_radius", float);
        UPDATE_SCALAR_FIELD("dash_period", float);
        UPDATE_SCALAR_FIELD("dash_length", float);
        UPDATE_MESSAGE_FIELD("line_empty_color");
        UPDATE_SCALAR_FIELD("animation_speed", float);
        return true;
    };

    auto migrate_heatmap_repr = [&](const DynamicMessage& src, DynamicMessage& dst) -> bool
    {
        UPDATE_SCALAR_FIELD("disc_radius", float);
        UPDATE_SCALAR_FIELD("value", float);
        return true;
    };

#undef UPDATE_SCALAR_FIELD
#undef UPDATE_MESSAGE_FIELD

    auto migrate_vector_tiles_layers = [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto src_style = src.get_message("style");
        auto dst_style = dst->get_message("style");

        size_t repr_count = dst_style.field_size("representations");
        for (size_t j = 0; j < repr_count; ++j)
        {
            auto src_repr = src_style.get_repeated_message("representations", j);
            auto dst_repr = dst_style.get_repeated_message("representations", j);

            if (src_repr.get_enum("type") == "NONE")
            {
                dst_repr.set_enum("type", "NULL_VECTOR_REPR");
            }
            else if (src_repr.get_enum("type") == "EXTRUDED_GEOMETRY")
            {
                dst_repr.set_enum("type", "EXTRUDED_GEOMETRY_VECTOR_REPR");
                auto src_extruded = src_repr.get_message("extruded_geometry");
                auto dst_extruded = dst_repr.get_message("extruded_geometry");
                migrate_extruded_repr(src_extruded, dst_extruded);
            }
            else if (src_repr.get_enum("type") == "SPRITE")
            {
                dst_repr.set_enum("type", "SPRITE_VECTOR_REPR");
                auto src_sprite = src_repr.get_message("sprite");
                auto dst_sprite = dst_repr.get_message("sprite");
                migrate_sprite_repr(src_sprite, dst_sprite);
            }
            else if (src_repr.get_enum("type") == "MODEL")
            {
                dst_repr.set_enum("type", "MODEL_VECTOR_REPR");
                auto src_model = src_repr.get_message("model");
                auto dst_model = dst_repr.get_message("model");
                migrate_model_repr(src_model, dst_model);
            }
            else if (src_repr.get_enum("type") == "STEM")
            {
                dst_repr.set_enum("type", "STEM_VECTOR_REPR");
                auto src_stem = src_repr.get_message("stem");
                auto dst_stem = dst_repr.get_message("stem");
                migrate_stem_repr(src_stem, dst_stem);
            }
            else if (src_repr.get_enum("type") == "CYLINDER")
            {
                dst_repr.set_enum("type", "CYLINDER_VECTOR_REPR");
                auto src_cylinder = src_repr.get_message("cylinder");
                auto dst_cylinder = dst_repr.get_message("cylinder");
                migrate_cylinder_repr(src_cylinder, dst_cylinder);
            }
            else if (src_repr.get_enum("type") == "TEXT_REPRESENTATION")
            {
                dst_repr.set_enum("type", "TEXT_VECTOR_REPR");
                auto src_text = src_repr.get_message("text");
                auto dst_text = dst_repr.get_message("text");
                migrate_text_repr(src_text, dst_text);
            }
            else if (src_repr.get_enum("type") == "FLAT_OVERLAY")
            {
                dst_repr.set_enum("type", "FLAT_OVERLAY_VECTOR_REPR");
                auto src_flat_overlay = src_repr.get_message("flat_overlay_geometry");
                auto dst_flat_overlay = dst_repr.get_message("flat_overlay_geometry");
                migrate_flat_overlay_repr(src_flat_overlay, dst_flat_overlay);
            }
            else if (src_repr.get_enum("type") == "HEATMAP")
            {
                dst_repr.set_enum("type", "HEATMAP_VECTOR_REPR");
                auto src_heatmap = src_repr.get_message("heatmap");
                auto dst_heatmap = dst_repr.get_message("heatmap");
                migrate_heatmap_repr(src_heatmap, dst_heatmap);
            }
        }

        return true;
    };

    return visit_layers_of_type("vector_tiles", src, dst, migrate_vector_tiles_layers);
}

// Update `color` property of extruded vector geometry to `upper_color` and `lower_color`
bool migration_7363df2a_to_b2e474b3(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_vector_tiles_layers = [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto src_style = src.get_message("style");
        auto dst_style = dst->get_message("style");

        const size_t repr_count = src_style.field_size("representations");

        for (size_t i = 0; i < repr_count; i++)
        {
            auto src_repr = src_style.get_repeated_message("representations", i);
            auto dst_repr = dst_style.get_repeated_message("representations", i);

            if (src_repr.get_enum("type") != "EXTRUDED_GEOMETRY_VECTOR_REPR")
            {
                continue;
            }

            auto src_extruded = src_repr.get_message("extruded_geometry");
            auto dst_extruded = dst_repr.get_message("extruded_geometry");

            auto src_color_prp = src_extruded.get_message("color");

            dst_extruded.copy_message("upper_color", src_color_prp);
            dst_extruded.copy_message("lower_color", src_color_prp);
            dst_extruded.copy_message("roof_color", src_color_prp);
        }

        return true;
    };

    return visit_layers_of_type("vector_tiles", src, dst, migrate_vector_tiles_layers);
}

bool migration_b2e474b3_to_a11b224d(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_vector_tiles_layers = [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto src_res = src.get_message("resolution");
        auto dst_res = dst->get_message("resolution");

        float sse = (float)src_res.get_uint32("max_screen_space_error");
        dst_res.set_float("max_screen_space_error", sse);

        return true;
    };

    return visit_layers_of_type("vector_tiles", src, dst, migrate_vector_tiles_layers);
}

// Replace gizmo 'active_component' field by the new list of component.
bool migration_a11b224d_to_49a872a7(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        if (src.get_enum("active_component") == "TRANSLATION_COMPONENT")
        {
            auto origin = dst->create_message("HrzProtocol.GizmoComponent");
            origin.set_enum("type", "GIZMO_ORIGIN");
            origin.set_enum("reference_frame", "WORLD_REFERENCE");
            auto axis_x = dst->create_message("HrzProtocol.GizmoComponent");
            axis_x.set_enum("type", "GIZMO_TRANSLATION_AXIS_X");
            axis_x.set_enum("reference_frame", "WORLD_REFERENCE");
            auto axis_y = dst->create_message("HrzProtocol.GizmoComponent");
            axis_y.set_enum("type", "GIZMO_TRANSLATION_AXIS_Y");
            axis_y.set_enum("reference_frame", "WORLD_REFERENCE");
            auto axis_z = dst->create_message("HrzProtocol.GizmoComponent");
            axis_z.set_enum("type", "GIZMO_TRANSLATION_AXIS_Z");
            axis_z.set_enum("reference_frame", "WORLD_REFERENCE");
            auto plane_x = dst->create_message("HrzProtocol.GizmoComponent");
            plane_x.set_enum("type", "GIZMO_TRANSLATION_PLANE_X");
            plane_x.set_enum("reference_frame", "WORLD_REFERENCE");
            auto plane_y = dst->create_message("HrzProtocol.GizmoComponent");
            plane_y.set_enum("type", "GIZMO_TRANSLATION_PLANE_Y");
            plane_y.set_enum("reference_frame", "WORLD_REFERENCE");
            auto plane_z = dst->create_message("HrzProtocol.GizmoComponent");
            plane_z.set_enum("type", "GIZMO_TRANSLATION_PLANE_Z");
            plane_z.set_enum("reference_frame", "WORLD_REFERENCE");
            auto camera_plane = dst->create_message("HrzProtocol.GizmoComponent");
            camera_plane.set_enum("type", "GIZMO_TRANSLATION_PLANE_CAMERA_PLANE");
            camera_plane.set_enum("reference_frame", "WORLD_REFERENCE");

            dst->add_repeated_message("components", origin);
            dst->add_repeated_message("components", axis_x);
            dst->add_repeated_message("components", axis_y);
            dst->add_repeated_message("components", axis_z);
            dst->add_repeated_message("components", plane_x);
            dst->add_repeated_message("components", plane_y);
            dst->add_repeated_message("components", plane_z);
            dst->add_repeated_message("components", camera_plane);
        }
        else if (src.get_enum("active_component") == "ROTATION_COMPONENT")
        {
            auto origin = dst->create_message("HrzProtocol.GizmoComponent");
            origin.set_enum("type", "GIZMO_ORIGIN");
            origin.set_enum("reference_frame", "WORLD_REFERENCE");
            auto ring_x = dst->create_message("HrzProtocol.GizmoComponent");
            ring_x.set_enum("type", "GIZMO_ROTATION_RING_X");
            ring_x.set_enum("reference_frame", "WORLD_REFERENCE");
            auto ring_y = dst->create_message("HrzProtocol.GizmoComponent");
            ring_y.set_enum("type", "GIZMO_ROTATION_RING_Y");
            ring_y.set_enum("reference_frame", "WORLD_REFERENCE");
            auto ring_z = dst->create_message("HrzProtocol.GizmoComponent");
            ring_z.set_enum("type", "GIZMO_ROTATION_RING_Z");
            ring_z.set_enum("reference_frame", "WORLD_REFERENCE");
            auto camera_plane = dst->create_message("HrzProtocol.GizmoComponent");
            camera_plane.set_enum("type", "GIZMO_ROTATION_RING_CAMERA_PLANE");
            camera_plane.set_enum("reference_frame", "WORLD_REFERENCE");

            dst->add_repeated_message("components", origin);
            dst->add_repeated_message("components", ring_x);
            dst->add_repeated_message("components", ring_y);
            dst->add_repeated_message("components", ring_z);
            dst->add_repeated_message("components", camera_plane);
        }

        return true;
    };

    return visit_layers_of_type("gizmo", src, dst, migrate_fn);
}

// Set the timeout for client vector data request to 5 seconds.
bool migration_49a872a7_to_89f0ca38(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        for (int i = 0; i < dst->field_size("sources"); ++i)
        {
            auto source = dst->get_repeated_message("sources", i);

            if (source.get_enum("provider_type") == "CLIENT_VECTOR_DATA_PROVIDER")
            {
                auto provider = source.get_message("client_data_provider");
                provider.set_float("timeout", 5.0f);
            }
        }

        return true;
    };

    return visit_layers_of_type("vector_data", src, dst, migrate_fn);
}

// Image and decorated shape fit modes
bool migration_89f0ca38_to_616ce918(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_layer_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto style = dst->get_message("style");
        int repr_count = style.field_size("representations");
        for (int i = 0; i < repr_count; ++i)
        {
            auto repr = style.get_repeated_message("representations", i);
            if (!repr.has_field("symbol")) continue;

            auto symbol = repr.get_message("symbol");

            walk_fields_of_type(
                "HrzProtocol.SymbolElement",
                src.get_message("style")
                    .get_repeated_message("representations", i)
                    .get_message("symbol"),
                &symbol,
                [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
                {
                    if (dst->get_enum("type") == "IMAGE_SYMBOL_ELEMENT")
                    {
                        dst->get_message("image").set_enum("fit_mode", "BOX_FIT_FILL");
                    }
                    else if (dst->get_enum("type") == "DECORATED_SHAPE_SYMBOL_ELEMENT")
                    {
                        dst->get_message("decorated_shape").set_enum("fit_mode", "BOX_FIT_FILL");
                    }
                    return true;
                });
        }
        return true;
    };
    return visit_layers_of_type("vector_tiles", src, dst, migrate_layer_fn);
}

bool migration_616ce918_to_7a5a4a2b(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_layer_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        dst->get_message("scene_views").set_uint32("bits", 3);
        return true;
    };
    return visit_layers_of_type("vector_tiles", src, dst, migrate_layer_fn);
}

// Symbol relative scale modes: replace the boolean properties by their new enum counterpart
bool migration_7a5a4a2b_to_433e3381(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_layer_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto style = dst->get_message("style");
        int repr_count = style.field_size("representations");
        for (int i = 0; i < repr_count; ++i)
        {
            auto repr = style.get_repeated_message("representations", i);
            if (!repr.has_field("symbol")) continue;

            auto symbol = repr.get_message("symbol");

            walk_fields_of_type(
                "HrzProtocol.SymbolElement",
                src.get_message("style")
                    .get_repeated_message("representations", i)
                    .get_message("symbol"),
                &symbol,
                [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
                {
                    if (dst->get_enum("type") == "ANCHOR_SYMBOL_ELEMENT")
                    {
                        auto src_anchor = src.get_message("anchor");
                        auto dst_anchor = dst->get_message("anchor");

                        dst_anchor.set_enum(
                            "position_offset_relative_scaling",
                            src_anchor.get_bool("position_offset_is_relative")
                                ? "SYMBOL_RELATIVE_SCALING_REF_DISTANCE"
                                : "SYMBOL_RELATIVE_SCALING_NONE");
                        dst_anchor.set_enum(
                            "element_size_relative_scaling",
                            src_anchor.get_bool("element_size_is_relative")
                                ? "SYMBOL_RELATIVE_SCALING_REF_DISTANCE"
                                : "SYMBOL_RELATIVE_SCALING_NONE");
                    }
                    return true;
                });
        }
        return true;
    };
    return visit_layers_of_type("vector_tiles", src, dst, migrate_layer_fn);
}

bool migration_433e3381_to_88908070(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_layer_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto style = dst->get_message("style");
        int repr_count = style.field_size("representations");
        for (int i = 0; i < repr_count; ++i)
        {
            auto repr = style.get_repeated_message("representations", i);
            if (!repr.has_field("symbol")) continue;

            auto symbol = repr.get_message("symbol");

            walk_fields_of_type(
                "HrzProtocol.AnchorSymbolElement",
                src.get_message("style")
                    .get_repeated_message("representations", i)
                    .get_message("symbol"),
                &symbol,
                [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
                {
                    dst->set_bool("can_overlap_other_symbols", true);
                    dst->set_bool("hides_other_symbols", false);
                    return true;
                });
        }
        return true;
    };
    return visit_layers_of_type("vector_tiles", src, dst, migrate_layer_fn);
}

bool migration_88908070_to_b6f4eb7c(const DynamicMessage& src, DynamicMessage* dst)
{
    walk_fields_of_type(
        "HrzProtocol.LightingSettings", src, dst,
        [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
        {
            bool enable_shadows = src.get_bool("enable_shadows");
            dst->set_bool("cast_shadows", enable_shadows);
            dst->set_bool("receive_shadows", enable_shadows);
            return true;
        });
    return true;
}

// Boolean properties.
bool migration_b6f4eb7c_to_65857045(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_layer_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto style = dst->get_message("style");
        int repr_count = style.field_size("representations");
        for (int i = 0; i < repr_count; ++i)
        {
            auto repr = style.get_repeated_message("representations", i);
            if (!repr.has_field("symbol")) continue;

            auto symbol = repr.get_message("symbol");

            walk_fields_of_type(
                "HrzProtocol.SymbolElement",
                src.get_message("style")
                    .get_repeated_message("representations", i)
                    .get_message("symbol"),
                &symbol,
                [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
                {
                    if (src.get_enum("type") == "OPTIONAL_SYMBOL_ELEMENT")
                    {
                        bool value = (bool)src.get_message("optional")
                                         .get_message("display_child")
                                         .get_int64("default_value");
                        dst->get_message("optional")
                            .get_message("display_child")
                            .set_bool("default_value", value);
                    }
                    return true;
                });
        }

        return true;
    };

    return visit_layers_of_type("vector_tiles", src, dst, migrate_layer_fn);
}

// Instanced models non uniform scaling
bool migration_65857045_to_8fa146fa(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_layer_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto style = dst->get_message("style");
        int repr_count = style.field_size("representations");
        for (int i = 0; i < repr_count; ++i)
        {
            auto repr = style.get_repeated_message("representations", i);
            if (!repr.has_field("model")) continue;

            auto dst_scale = repr.get_message("model").get_message("scale");
            auto src_scale = src.get_message("style")
                                 .get_repeated_message("representations", i)
                                 .get_message("model")
                                 .get_message("scale");

            dst_scale.set_string("name", src_scale.get_string("name"));
            for (auto axis : {"x", "y", "z"})
            {
                dst_scale.get_message("default_value")
                    .set_float(axis, src_scale.get_float("default_value"));
            }
        }
        return true;
    };
    return visit_layers_of_type("vector_tiles", src, dst, migrate_layer_fn);
}

bool migration_8fa146fa_to_cbbac550(const DynamicMessage& src, DynamicMessage* dst)
{
    return walk_fields_of_type(
        "HrzProtocol.AmbientSettings", src, dst,
        [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
        {
            auto dst_sky = dst->get_message("sky");
            auto dst_sun = dst->get_message("sun");
            auto dst_ambient = dst->get_message("ambient_lighting");

            if (src.get_bool("enable_atmosphere"))
            {
                dst_sun.set_enum("mode", "SUN_LIGHTING_SIMULATED");
                dst_ambient.set_enum("mode", "AMBIENT_LIGHTING_SIMULATED");
                dst_sky.set_enum("mode", "SKY_SIMULATED");
            }
            else
            {
                dst_sun.set_enum("mode", "SUN_LIGHTING_STATIC");
                dst_ambient.set_enum("mode", "AMBIENT_LIGHTING_STATIC");
                dst_sky.set_enum("mode", "SKY_STATIC");
            }

            dst_sky.set_float("attenuation", src.get_float("atmosphere_attenuation"));

            float ambient_strength = src.get_float("ambient_strength");
            float sun_strength = src.get_float("sun_strength");

            if (sun_strength > ambient_strength)
            {
                dst->set_float("lighting_strength", sun_strength);
                dst->set_float("sun_ambient_balance", 1.0 - 0.5 * ambient_strength / sun_strength);
            }
            else
            {
                dst->set_float("lighting_strength", ambient_strength);
                dst->set_float("sun_ambient_balance", 0.5 * sun_strength / ambient_strength);
            }

            dst->set_float("wrap_lighting", 0.0f);

            auto dst_sun_color = dst_sun.get_message("static_color");
            dst_sun_color.set_float("r", 1.0f);
            dst_sun_color.set_float("g", 1.0f);
            dst_sun_color.set_float("b", 1.0f);
            dst_sun_color.set_float("a", 1.0f);

            auto dst_sun_direction = dst_sun.get_message("direction");
            dst_sun_direction.set_enum("mode", "SUN_DIRECTION_RELATIVE_TO_DATE");
            dst_sun_direction.set_float("local_solar_time", src.get_float("local_solar_time"));
            dst_sun_direction.set_float("day_of_year", src.get_float("day_of_year"));
            dst_sun_direction.set_float("azimuth", 0.0);
            dst_sun_direction.set_float("altitude", 0.0);

            auto dst_sky_color = dst_sky.get_message("static_color");
            dst_sky_color.set_float("r", src.get_message("sky_color").get_float("r"));
            dst_sky_color.set_float("g", src.get_message("sky_color").get_float("g"));
            dst_sky_color.set_float("b", src.get_message("sky_color").get_float("b"));
            dst_sky_color.set_float("a", src.get_message("sky_color").get_float("a"));

            auto dst_ambient_color = dst_ambient.get_message("static_color");
            dst_ambient_color.set_float("r", src.get_message("ambient_color").get_float("r"));
            dst_ambient_color.set_float("g", src.get_message("ambient_color").get_float("g"));
            dst_ambient_color.set_float("b", src.get_message("ambient_color").get_float("b"));
            dst_ambient_color.set_float("a", src.get_message("ambient_color").get_float("a"));

            auto primary_fog = dst->get_message("primary_fog");
            {
                primary_fog.set_float("density", 1.0f);
                primary_fog.set_float("start_distance", 1000.0f);
                primary_fog.set_float("falloff_start", 0.0f);
                primary_fog.set_float("falloff_end", 300.0f);
                primary_fog.set_bool("apply_to_sky", true);

                auto color = primary_fog.get_message("color");
                color.set_float("r", 1.0);
                color.set_float("g", 1.0);
                color.set_float("b", 1.0);
                color.set_float("a", 0.0);
            }

            auto secondary_fog = dst->get_message("secondary_fog");
            {
                secondary_fog.set_float("density", 2.0f);
                secondary_fog.set_float("start_distance", 300.0f);
                secondary_fog.set_float("falloff_start", 0.0f);
                secondary_fog.set_float("falloff_end", 300.0f);
                secondary_fog.set_bool("apply_to_sky", true);

                auto color = secondary_fog.get_message("color");
                color.set_float("r", 1.0);
                color.set_float("g", 1.0);
                color.set_float("b", 1.0);
                color.set_float("a", 0.0);
            }

            return true;
        });
}

// Move min-max levels and bounds from vector data layers to their sources
bool migration_cbbac550_to_8f07359d(const DynamicMessage& src, DynamicMessage* dst)
{
    struct VectorDataLayerGeometry
    {
        uint32_t min_level;
        uint32_t max_level;
        double west;
        double south;
        double east;
        double north;
    };

    std::unordered_map<uint32_t, VectorDataLayerGeometry> geometry_by_layer_id;

    // Move the geometry from the layer to its sources.
    // Save the geometry if there is a in-memory vector data provider and no other
    // source of geometry.
    auto migrate_vector_data_layer_fn = [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        uint32_t layer_id = src.get_uint32("id");

        VectorDataLayerGeometry geometry;
        geometry.min_level = src.get_uint32("min_level");
        geometry.max_level = src.get_uint32("max_level");
        geometry.west = src.get_message("bounds").get_double("west");
        geometry.south = src.get_message("bounds").get_double("south");
        geometry.east = src.get_message("bounds").get_double("east");
        geometry.north = src.get_message("bounds").get_double("north");

        auto set_bounds = [&](DynamicMessage msg)
        {
            msg.set_uint32("min_level", geometry.min_level);
            msg.set_uint32("max_level", geometry.max_level);
            auto bounds = msg.create_message("HrzProtocol.GeographicBounds");
            bounds.set_double("west", geometry.west);
            bounds.set_double("south", geometry.south);
            bounds.set_double("east", geometry.east);
            bounds.set_double("north", geometry.north);
            msg.set_message("bounds", bounds);
        };

        bool geometry_is_needed = false;
        bool geometry_is_provided = false;

        for (int i = 0; i < dst->field_size("sources"); ++i)
        {
            auto dst_source = dst->get_repeated_message("sources", i);

            if (dst_source.get_enum("provider_type") == "TILED_VECTOR_DATA_PROVIDER")
            {
                set_bounds(dst_source.get_message("tiled_data_provider"));
                geometry_is_provided = true;
            }
            else if (dst_source.get_enum("provider_type") == "CLIENT_VECTOR_DATA_PROVIDER")
            {
                if (dst_source.get_enum("access") == "ACCESS_BY_TILE")
                {
                    set_bounds(dst_source.get_message("client_data_provider"));
                    geometry_is_provided = true;
                }
            }
            else if (dst_source.get_enum("provider_type") == "IN_MEMORY_VECTOR_DATA_PROVIDER")
            {
                geometry_is_needed = true;
            }
            else if (dst_source.get_enum("provider_type") == "TILEJSON_VECTOR_DATA_PROVIDER")
            {
                geometry_is_provided = true;
            }
            else if (dst_source.get_enum("provider_type") == "UNTILED_VECTOR_DATA_PROVIDER")
            {
                set_bounds(dst_source.get_message("untiled_data_provider"));
                geometry_is_provided = true;
            }
        }

        if (geometry_is_needed && !geometry_is_provided)
        {
            if (geometry_by_layer_id.find(layer_id) == geometry_by_layer_id.end())
            {
                geometry_by_layer_id.insert({layer_id, geometry});
            }
        }

        return true;
    };

    // Override the geometry for vector tiles layers that use data from
    // in-memory vector data providers with no other source of geometry.
    auto migrate_vector_tiles_layer_fn = [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto dst_source = dst->get_message("source");
        auto source_layer_id = dst_source.get_uint32("vector_data_layer_id");

        auto it = geometry_by_layer_id.find(source_layer_id);
        if (it != geometry_by_layer_id.end())
        {
            const auto& geometry = it->second;

            if (!dst_source.get_bool("override_levels"))
            {
                dst_source.set_bool("override_levels", true);
                dst_source.set_uint32("min_level", geometry.min_level);
                dst_source.set_uint32("max_level", geometry.max_level);
            }

            if (!dst_source.get_bool("override_bounds"))
            {
                dst_source.set_bool("override_bounds", true);
                auto bounds = dst_source.create_message("HrzProtocol.GeographicBounds");
                bounds.set_double("west", geometry.west);
                bounds.set_double("south", geometry.south);
                bounds.set_double("east", geometry.east);
                bounds.set_double("north", geometry.north);
                dst_source.set_message("bounds", bounds);
            }
        }

        return true;
    };

    if (!visit_layers_of_type("vector_data", src, dst, migrate_vector_data_layer_fn)) return false;
    return visit_layers_of_type("vector_tiles", src, dst, migrate_vector_tiles_layer_fn);
}

// Per-instance text properties:
// * Outline width,
//   * Explicitly set the unit,
//   * Double the previous value,
// * Line spacing,
// * Alignment.
bool migration_8f07359d_to_592a0391(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_layer_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto src_style = src.get_message("style");
        auto dst_style = dst->get_message("style");

        size_t repr_count = dst_style.field_size("representations");
        for (size_t j = 0; j < repr_count; ++j)
        {
            auto src_repr = src_style.get_repeated_message("representations", j);
            auto dst_repr = dst_style.get_repeated_message("representations", j);

            if (src_repr.get_enum("type") == "SYMBOL_VECTOR_REPR")
            {
                auto src_symbol = src_repr.get_message("symbol");
                auto dst_symbol = dst_repr.get_message("symbol");

                walk_fields_of_type(
                    "HrzProtocol.SymbolElement", src_symbol, &dst_symbol,
                    [&](const DynamicMessage& src_element, DynamicMessage* dst_element) -> bool
                    {
                        if (src_element.get_enum("type") == "TEXT_SYMBOL_ELEMENT")
                        {
                            auto src_text = src_element.get_message("text");
                            auto dst_text = dst_element->get_message("text");

                            float outline_width = src_text.get_float("outline_width");
                            outline_width *= 2.0f;
                            auto outline_width_property =
                                dst_text.create_message("HrzProtocol.FloatProperty");
                            outline_width_property.set_float("default_value", outline_width);
                            dst_text.set_message("outline_width", outline_width_property);
                            dst_text.set_enum("outline_width_unit", "OUTLINE_WIDTH_IN_EM");

                            float line_spacing = src_text.get_float("line_spacing");
                            auto line_spacing_property =
                                dst_text.create_message("HrzProtocol.FloatProperty");
                            line_spacing_property.set_float("default_value", line_spacing);
                            dst_text.set_message("line_spacing", line_spacing_property);

                            auto alignment = src_text.get_enum("alignment");
                            auto alignment_property =
                                dst_text.create_message("HrzProtocol.TextAlignmentProperty");
                            alignment_property.set_enum("default_value", alignment);
                            dst_text.set_message("alignment", alignment_property);
                        }
                        return true;
                    });
            }
        }
        return true;
    };
    return visit_layers_of_type("vector_tiles", src, dst, migrate_layer_fn);
}

// Add blur size to heatmap representation
bool migration_592a0391_to_ee415b5e(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_layer_fn = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto dst_style = dst->get_message("style");

        size_t repr_count = dst_style.field_size("representations");
        for (size_t j = 0; j < repr_count; ++j)
        {
            auto dst_repr = dst_style.get_repeated_message("representations", j);

            if (dst_repr.get_enum("type") == "HEATMAP_VECTOR_REPR")
            {
                auto dst_heatmap = dst_repr.get_message("heatmap");
                dst_heatmap.set_float("blur_size", 1.2f);

                auto radius = dst_heatmap.get_message("disc_radius");
                radius.set_float("default_value", radius.get_float("default_value") * 0.5f);
            }
        }
        return true;
    };
    return visit_layers_of_type("vector_tiles", src, dst, migrate_layer_fn);
}

// Adds the appropriate attribute transform for attributes of old type COLOR.
bool migration_ee415b5e_to_4e6adf6e(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_3dt_layer = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto count = src.field_size("attributes");
        if (count != dst->field_size("attributes")) return false;

        for (int i = 0; i < count; ++i)
        {
            if (src.get_repeated_message("attributes", i).get_enum("type") == "COLOR_ATTRIBUTE")
            {
                dst->get_repeated_message("attributes", i)
                    .set_enum("transform", "ATTRIBUTE_TRANSFORM_TO_COLOR");
            }
            else if (
                src.get_repeated_message("attributes", i).get_enum("type") == "BOOLEAN_ATTRIBUTE")
            {
                dst->get_repeated_message("attributes", i)
                    .set_enum("transform", "ATTRIBUTE_TRANSFORM_TO_BOOL");
            }
        }

        return true;
    };

    auto migrate_vd_layer = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto source_count = src.field_size("sources");
        if (source_count != dst->field_size("sources")) return false;

        for (int source_i = 0; source_i < source_count; ++source_i)
        {
            const auto src_source = src.get_repeated_message("sources", source_i);
            auto dst_source = dst->get_repeated_message("sources", source_i);

            auto attr_count = src_source.field_size("attributes");
            if (attr_count != dst_source.field_size("attributes")) return false;

            for (int attr_i = 0; attr_i < attr_count; ++attr_i)
            {
                if (src_source.get_repeated_message("attributes", attr_i).get_enum("type")
                    == "COLOR_ATTRIBUTE")
                {
                    dst_source.get_repeated_message("attributes", attr_i)
                        .set_enum("transform", "ATTRIBUTE_TRANSFORM_TO_COLOR");
                }
                else if (
                    src_source.get_repeated_message("attributes", attr_i).get_enum("type")
                    == "BOOLEAN_ATTRIBUTE")
                {
                    dst_source.get_repeated_message("attributes", attr_i)
                        .set_enum("transform", "ATTRIBUTE_TRANSFORM_TO_BOOL");
                }
            }
        }

        return true;
    };

    auto migrate_im_layer = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto feature_count = src.field_size("features");
        if (feature_count != dst->field_size("features")) return false;

        for (int feature_i = 0; feature_i < feature_count; ++feature_i)
        {
            const auto src_feature = src.get_repeated_message("features", feature_i);
            auto dst_feature = dst->get_repeated_message("features", feature_i);

            auto attr_count = src_feature.field_size("attribute_values");
            if (attr_count != dst_feature.field_size("attribute_values")) return false;

            for (int attr_i = 0; attr_i < attr_count; ++attr_i)
            {
                auto src_attr = src_feature.get_repeated_message("attribute_values", attr_i);
                auto dst_attr = dst_feature.get_repeated_message("attribute_values", attr_i);

                if (src_attr.has_field("int_value") && src_attr.get_int64("int_value"))
                {
                    dst_attr.set_sfixed64("int64_value", src_attr.get_int64("int_value"));
                }
                else if (src_attr.has_field("uint_value") && src_attr.get_uint64("uint_value"))
                {
                    dst_attr.set_fixed64("uint64_value", src_attr.get_uint64("uint_value"));
                }
                else if (
                    src_attr.has_field("color_value")
                    && !src_attr.get_string("color_value").empty())
                {
                    dst_attr.set_string("string_value", src_attr.get_string("color_value"));
                }
            }
        }

        auto attr_count = src.field_size("attributes");
        if (attr_count != dst->field_size("attributes")) return false;

        for (int attr_i = 0; attr_i < attr_count; ++attr_i)
        {
            if (src.get_repeated_message("attributes", attr_i).get_enum("type")
                == "COLOR_ATTRIBUTE")
            {
                dst->get_repeated_message("attributes", attr_i)
                    .set_enum("transform", "ATTRIBUTE_TRANSFORM_TO_COLOR");
            }
            else if (
                src.get_repeated_message("attributes", attr_i).get_enum("type")
                == "BOOLEAN_ATTRIBUTE")
            {
                dst->get_repeated_message("attributes", attr_i)
                    .set_enum("transform", "ATTRIBUTE_TRANSFORM_TO_BOOL");
            }
        }

        return true;
    };

    if (!visit_layers_of_type("three_d_tiles", src, dst, migrate_3dt_layer)) return false;
    if (!visit_layers_of_type("vector_data", src, dst, migrate_vd_layer)) return false;
    if (!visit_layers_of_type("in_memory_vector_source", src, dst, migrate_im_layer)) return false;

    return true;
}

// Adds the default camera ground collision values
bool migration_4e6adf6e_to_e252ad08(const DynamicMessage& src, DynamicMessage* dst)
{
    int camera_count = dst->field_size("camera_settings");
    for (int i = 0; i < camera_count; ++i)
    {
        auto camera_settings =
            dst->get_repeated_message("camera_settings", i).get_message("settings");
        camera_settings.set_double("min_height_above_terrain", 2.0);
        camera_settings.set_float("terrain_collision_inertia", 0.06);
    }

    return true;
}

// Creates RasterProvider instances.
// Moves geometry and nodata into raster providers.
bool migration_e252ad08_to_2c75ee8f(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_layer = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        auto src_raster = src.get_message("raster");
        auto src_raster_params = src_raster.get_message("params");
        auto src_sampling = src_raster_params.get_message("sampling");
        auto src_nodata_value = src_sampling.get_message("nodata_value");

        auto copy_nodata_to_provider = [&](DynamicMessage& dst_provider_params)
        {
            auto nodata = dst->create_message("HrzProtocol.RasterNodata");
            nodata.set_bool("has_nodata", src_nodata_value.field_size("value") > 0);
            nodata.copy_message("value", src_nodata_value);
            dst_provider_params.set_message("nodata", nodata);
        };

        std::function<DynamicMessage(const DynamicMessage&, bool)> migrate_provider;
        migrate_provider = [&](const DynamicMessage& src_message, bool copy_nodata)
        {
            auto dst_provider = dst->create_message("HrzProtocol.RasterProvider");
            auto provider_type = src_message.get_enum("provider_type");
            dst_provider.set_enum("type", provider_type);
            if (provider_type == "SINGLE_IMAGE_PROVIDER")
            {
                dst_provider.copy_message(
                    "single_image", src_message.get_message("single_image_provider"));
                auto single_image = dst_provider.get_message("single_image");
                single_image.copy_message("geometry", src_raster_params.get_message("geometry"));
                if (copy_nodata) copy_nodata_to_provider(single_image);
            }
            else if (provider_type == "TILED_IMAGE_PROVIDER")
            {
                dst_provider.copy_message(
                    "tiled_image", src_message.get_message("tiled_image_provider"));
                auto tiled_image = dst_provider.get_message("tiled_image");
                tiled_image.copy_message("geometry", src_raster_params.get_message("geometry"));
                if (copy_nodata) copy_nodata_to_provider(tiled_image);
            }
            else if (provider_type == "BING_PROVIDER")
            {
                dst_provider.copy_message("bing", src_message.get_message("bing_provider"));
            }
            else if (provider_type == "PALETTIZED_IMAGE_PROVIDER")
            {
                auto src_provider = src_message.get_message("palettized_image_provider");
                dst_provider.copy_message(
                    "palettized_image", src_message.get_message("palettized_image_provider"));
                dst_provider.get_message("palettized_image")
                    .set_message("provider", migrate_provider(src_provider, false));
                auto palettized_image = dst_provider.get_message("palettized_image");
                if (copy_nodata)
                {
                    copy_nodata_to_provider(palettized_image);
                    if (src_nodata_value.field_size("value") >= 4)
                    {
                        auto dst_nodata_color = palettized_image.get_message("nodata_color");
                        dst_nodata_color.set_float(
                            "r", src_nodata_value.get_repeated_int32("value", 0) / 255.0f);
                        dst_nodata_color.set_float(
                            "g", src_nodata_value.get_repeated_int32("value", 1) / 255.0f);
                        dst_nodata_color.set_float(
                            "b", src_nodata_value.get_repeated_int32("value", 2) / 255.0f);
                        dst_nodata_color.set_float(
                            "a", src_nodata_value.get_repeated_int32("value", 3) / 255.0f);
                    }
                }
            }
            else if (provider_type == "ARCGIS_PROVIDER")
            {
                dst_provider.copy_message("arcgis", src_message.get_message("arcgis_provider"));
            }
            else if (provider_type == "TMS_PROVIDER")
            {
                dst_provider.copy_message("tms", src_message.get_message("tms_provider"));
                auto tms = dst_provider.get_message("tms");
                if (copy_nodata) copy_nodata_to_provider(tms);
            }
            else if (provider_type == "WMTS_PROVIDER")
            {
                dst_provider.copy_message("wmts", src_message.get_message("wmts_provider"));
                auto wmts = dst_provider.get_message("wmts");
                if (copy_nodata) copy_nodata_to_provider(wmts);
            }
            else if (provider_type == "WMS_PROVIDER")
            {
                dst_provider.copy_message("wms", src_message.get_message("wms_provider"));
                auto wms = dst_provider.get_message("wms");
                if (copy_nodata) copy_nodata_to_provider(wms);
            }
            else if (provider_type == "CESIUM_TERRAIN_PROVIDER")
            {
                dst_provider.copy_message(
                    "cesium_terrain", src_message.get_message("cesium_terrain_provider"));
            }
            else if (provider_type == "TILEJSON_PROVIDER")
            {
                dst_provider.copy_message("tilejson", src_message.get_message("tilejson_provider"));
                auto tilejson = dst_provider.get_message("tilejson");
                if (copy_nodata) copy_nodata_to_provider(tilejson);
            }
            else if (provider_type == "PMTILES_PROVIDER")
            {
                dst_provider.copy_message("pmtiles", src_message.get_message("pmtiles_provider"));
                auto pmtiles = dst_provider.get_message("pmtiles");
                if (copy_nodata) copy_nodata_to_provider(pmtiles);
            }
            else
            {
                assert(false);
            }
            return dst_provider;
        };

        auto dst_raster = dst->get_message("raster");

        dst_raster.copy_message("sampling", src_raster_params.get_message("sampling"));
        dst_raster.copy_message("blending", src_raster_params.get_message("blending"));
        dst_raster.copy_message("display_bounds", src_raster_params.get_message("display_bounds"));

        dst_raster.set_message("provider", migrate_provider(src_raster, true));

        return true;
    };

    if (!visit_layers_of_type("imagery_raster", src, dst, migrate_layer)) return false;
    if (!visit_layers_of_type("dtm_raster", src, dst, migrate_layer)) return false;

    return true;
}

bool migration_2c75ee8f_to_db6a65c4(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_raster_nodata = [](const DynamicMessage& src_raster_nodata,
                                    DynamicMessage& dst_raster_nodata,
                                    const std::string& image_format)
    {
        if (!src_raster_nodata.get_bool("has_nodata")) return;

        auto src_nodata_value = src_raster_nodata.get_message("value");
        auto dst_nodata_value = dst_raster_nodata.get_message("value");

        if (src_nodata_value.get_bool("nan"))
        {
            dst_nodata_value.set_enum("type", "NAN_NODATA");
            return;
        }

        if (image_format == "SRGBA_8")
        {
            std::array<uint8_t, 4> rgba = {0, 0, 0, 0};
            for (int i = 0; i < src_nodata_value.field_size("value") && i < 4; ++i)
            {
                rgba[i] = src_nodata_value.get_repeated_int32("value", i);
            }

            auto color = dst_nodata_value.create_message("HrzProtocol.Color");
            color.set_float("r", rgba[0] / 255.0f);
            color.set_float("g", rgba[1] / 255.0f);
            color.set_float("b", rgba[2] / 255.0f);
            color.set_float("a", rgba[3] / 255.0f);
            dst_nodata_value.set_message("color", color);
            dst_nodata_value.set_enum("type", "COLOR_NODATA");
        }
        else if (image_format == "SIGNED_FIXED_24_8")
        {
            int32_t nodata_value = 0;
            if (src_nodata_value.field_size("value") >= 1)
            {
                nodata_value = src_nodata_value.get_repeated_int32("value", 0);
            }

            if (nodata_value % 256 == 0)
            {
                dst_nodata_value.set_enum("type", "INT_VALUE_NODATA");
                dst_nodata_value.set_int32("int_value", nodata_value / 256);
            }
            else
            {
                dst_nodata_value.set_enum("type", "BIT_PATTERN_NODATA");
                dst_nodata_value.set_uint32("bit_pattern", (uint32_t)nodata_value);
            }
        }
        else
        {
            uint32_t nodata_pattern = 0;
            if (src_nodata_value.field_size("value") >= 1)
            {
                nodata_pattern = (uint32_t)src_nodata_value.get_repeated_int32("value", 0);
            }

            dst_nodata_value.set_enum("type", "BIT_PATTERN_NODATA");
            dst_nodata_value.set_uint32("bit_pattern", nodata_pattern);
        }
    };

    auto migrate_layer = [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        return walk_fields_of_type(
            "HrzProtocol.RasterProvider", src, dst,
            [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
            {
                auto provider_type = src.get_enum("type");
                if (provider_type == "TILED_IMAGE_PROVIDER")
                {
                    auto src_provider = src.get_message("tiled_image");
                    auto dst_provider = dst->get_message("tiled_image");
                    auto image_format = src_provider.get_enum("image_format");
                    auto src_nodata = src_provider.get_message("nodata");
                    auto dst_nodata = dst_provider.get_message("nodata");
                    migrate_raster_nodata(src_nodata, dst_nodata, image_format);
                }
                else if (provider_type == "SINGLE_IMAGE_PROVIDER")
                {
                    auto src_provider = src.get_message("single_image");
                    auto dst_provider = dst->get_message("single_image");
                    auto image_format = src_provider.get_enum("image_format");
                    auto src_nodata = src_provider.get_message("nodata");
                    auto dst_nodata = dst_provider.get_message("nodata");
                    migrate_raster_nodata(src_nodata, dst_nodata, image_format);
                }
                else if (provider_type == "PALETTIZED_IMAGE_PROVIDER")
                {
                    auto src_provider = src.get_message("palettized_image");
                    auto dst_provider = dst->get_message("palettized_image");
                    auto image_format = std::string("SRGBA_8");
                    auto src_nodata = src_provider.get_message("nodata");
                    auto dst_nodata = dst_provider.get_message("nodata");
                    migrate_raster_nodata(src_nodata, dst_nodata, image_format);
                }
                else if (provider_type == "TMS_PROVIDER")
                {
                    auto src_provider = src.get_message("tms");
                    auto dst_provider = dst->get_message("tms");
                    auto image_format = src_provider.get_enum("image_format");
                    auto src_nodata = src_provider.get_message("nodata");
                    auto dst_nodata = dst_provider.get_message("nodata");
                    migrate_raster_nodata(src_nodata, dst_nodata, image_format);
                }
                else if (provider_type == "WMTS_PROVIDER")
                {
                    auto src_provider = src.get_message("wmts");
                    auto dst_provider = dst->get_message("wmts");
                    auto image_format = src_provider.get_enum("image_format");
                    auto src_nodata = src_provider.get_message("nodata");
                    auto dst_nodata = dst_provider.get_message("nodata");
                    migrate_raster_nodata(src_nodata, dst_nodata, image_format);
                }
                else if (provider_type == "WMS_PROVIDER")
                {
                    auto src_provider = src.get_message("wms");
                    auto dst_provider = dst->get_message("wms");
                    auto image_format = src_provider.get_enum("image_format");
                    auto src_nodata = src_provider.get_message("nodata");
                    auto dst_nodata = dst_provider.get_message("nodata");
                    migrate_raster_nodata(src_nodata, dst_nodata, image_format);
                }
                else if (provider_type == "TILEJSON_PROVIDER")
                {
                    auto src_provider = src.get_message("tilejson");
                    auto dst_provider = dst->get_message("tilejson");
                    auto image_format = src_provider.get_enum("image_format");
                    auto src_nodata = src_provider.get_message("nodata");
                    auto dst_nodata = dst_provider.get_message("nodata");
                    migrate_raster_nodata(src_nodata, dst_nodata, image_format);
                }
                else if (provider_type == "PMTILES_PROVIDER")
                {
                    auto src_provider = src.get_message("pmtiles");
                    auto dst_provider = dst->get_message("pmtiles");
                    auto image_format = src_provider.get_enum("image_format");
                    auto src_nodata = src_provider.get_message("nodata");
                    auto dst_nodata = dst_provider.get_message("nodata");
                    migrate_raster_nodata(src_nodata, dst_nodata, image_format);
                }
                return true;
            });
    };

    if (!visit_layers_of_type("imagery_raster", src, dst, migrate_layer)) return false;
    if (!visit_layers_of_type("dtm_raster", src, dst, migrate_layer)) return false;

    return true;
}

// Move the tiling_scheme field from RasterGeometry to TiledImageRasterProviderParams
bool migration_db6a65c4_to_b232d003(const DynamicMessage& src, DynamicMessage* dst)
{
    auto migrate_layer = [](const DynamicMessage& src, DynamicMessage* dst) -> bool
    {
        return walk_fields_of_type(
            "HrzProtocol.RasterProvider", src, dst,
            [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
            {
                const auto& provider_type = src.get_enum("type");
                if (provider_type == "TILED_IMAGE_PROVIDER")
                {
                    dst->get_message("tiled").copy_message(
                        "tiling_scheme",
                        src.get_message("tiled_image")
                            .get_message("geometry")
                            .get_message("tiling_scheme"));
                }
                return true;
            });
    };

    if (!visit_layers_of_type("imagery_raster", src, dst, migrate_layer)) return false;
    if (!visit_layers_of_type("dtm_raster", src, dst, migrate_layer)) return false;
    return true;
}

bool migration_b232d003_to_1814b7c1(const DynamicMessage& src, DynamicMessage* dst)
{
    return walk_fields_of_type(
        "HrzProtocol.AmbientSettings", src, dst,
        [&](const DynamicMessage& src, DynamicMessage* dst) -> bool
        {
            const auto& src_sky = src.get_message("sky");
            auto dst_sky = dst->get_message("sky");

            const auto& src_static_color = src_sky.get_message("static_color");
            auto dst_atmosphere_color = dst_sky.get_message("static_atmosphere_color");
            dst_atmosphere_color.set_float("r", src_static_color.get_float("r"));
            dst_atmosphere_color.set_float("g", src_static_color.get_float("g"));
            dst_atmosphere_color.set_float("b", src_static_color.get_float("b"));
            dst_atmosphere_color.set_float("a", src_static_color.get_float("a"));

            auto dst_space_color = dst_sky.get_message("static_space_color");
            dst_space_color.set_float("r", 0.0);
            dst_space_color.set_float("g", 0.0);
            dst_space_color.set_float("b", 0.0);
            dst_space_color.set_float("a", 1.0);

            dst_sky.set_double("static_color_transition_start_distance", 15000.0);
            dst_sky.set_double("static_color_transition_end_distance", 60000.0);
            dst_sky.set_enum(
                "static_color_transition_distance_unit", "STATIC_SKY_COLOR_TRANSITION_UNIT_METERS");

            return true;
        });
}
} // namespace hrz::migration
