#pragma once

#include <hrz_common_color.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_inlined_vector.h>
#include <hrz_fnd_static_vector.h>
#include <hrz_fnd_string_utils.h>
#include <hrz_protocol_all.h>

#include <optional>
#include <span>
#include <string_view>

namespace hrz_mapbox
{
enum class MapboxVectorReprType
{
    Fill,
    Line,
    Symbol,
    Circle,
    FillExtrusion,
    Heatmap,
    Model,
};

enum class MapboxPropertyType
{
    // These can be mapped directly to a Horizon attribute
    Number,
    Bool,
    String,
    Color,
    // A formatted string cannot be used as a substitution for a string, but a string
    // can be used as a substitution for a formatted string.
    FormattedString,

    // But not those!
    NumberArray,
    StringArray,

    // We can't always deduce the types of operator operands.
    // For instance, "to-string" accepts many input types.
    // Which is very problematic when attributes arrive, as their types is supposed to be
    // deduced from the expected property type...
    // Those types must be handled their own specific way.
    Any,
};

enum class InterpolationType
{
    Linear,
    Exponential,
    CubicBezier,
};

static inline std::optional<MapboxPropertyType> mapbox_property_type_array_to_component_type(
    MapboxPropertyType type)
{
    switch (type)
    {
        case MapboxPropertyType::NumberArray: return MapboxPropertyType::Number;
        case MapboxPropertyType::StringArray: return MapboxPropertyType::String;

        default: return std::nullopt;
    }
}

struct VectorLayer
{
    std::string name;
    hrz_proto::VectorTilesLayer model;
};

struct VectorSource
{
    std::string name;
    hrz_proto::VectorDataLayer model;

    // Mapbox layers that use this data source.
    std::vector<VectorLayer> users;

    // @Todo(qdebroise): keep layer name to feature ID reference (Mapbox's `promoteId` stuff).

    struct Attribute
    {
        uint32_t id;
    };

    using AttributeMap = hrz::flat_hash_map<std::string, Attribute>;
    AttributeMap attributes;
};

// For strings, only a view to the string is contained.
// As such, instances of this structure must not outlive the JSON object they are extracted from.
struct Value
{
    enum class Type
    {
        Bool,
        UInt,
        Double,
        String,
    };

    Type type;

    union
    {
        bool b64;
        uint64_t u64;
        double f64;
        std::string_view str = "";
    };

#define GENERATE_STATIC_CONSTRUCTOR(CPP_TYPE, TYPE, FIELD) \
    static inline Value from(CPP_TYPE value)               \
    {                                                      \
        Value v;                                           \
        v.type = Type::TYPE;                               \
        v.FIELD = value;                                   \
        return v;                                          \
    }

    GENERATE_STATIC_CONSTRUCTOR(bool, Bool, b64)
    GENERATE_STATIC_CONSTRUCTOR(uint64_t, UInt, u64)
    GENERATE_STATIC_CONSTRUCTOR(double, Double, f64)
    GENERATE_STATIC_CONSTRUCTOR(std::string_view, String, str)
    GENERATE_STATIC_CONSTRUCTOR(const char*, String, str)

#undef GENERATE_STATIC_CONSTRUCTOR
};

template<typename T>
static inline T get_value(const Value& value);

template<>
uint64_t get_value<uint64_t>(const Value& value)
{
    return value.u64;
}

template<>
double get_value<double>(const Value& value)
{
    return value.f64;
}

template<typename T>
static inline void set_value(Value& value, T v);

template<>
void set_value<uint64_t>(Value& value, uint64_t v)
{
    value.u64 = v;
}

template<>
void set_value<double>(Value& value, double v)
{
    value.f64 = v;
}

template<typename T>
static inline Value::Type get_value_type();

template<>
Value::Type get_value_type<double>()
{
    return Value::Type::Double;
}

static inline std::string to_string(Value value)
{
    switch (value.type)
    {
        case Value::Type::Bool: return value.b64 ? "true" : "false";
        // UInt values are used for colors, so we format them in hexadecimal
        case Value::Type::UInt: return fmt::format("{:#x}", value.u64);
        // Note: we prefer using fmt rather than std::to_string for the nicer formatting
        // (123.0 will result in 123 for fmt, or 123.000000 for std::to_string).
        case Value::Type::Double:
            if (std::floor(value.f64) == value.f64)
            {
                // If the value has no decimal part, we still want to write
                // ".0" so that it doesn't get interpreted as an integer
                return fmt::format("{:.1f}", value.f64);
            }
            else
            {
                return fmt::format("{}", value.f64);
            }
        case Value::Type::String: return std::string(value.str.data(), value.str.size());
        default:
        {
            assert(false && "Unhandled case");
            break;
        }
    }

    return "";
}

static bool operator==(const Value& lhs, const Value& rhs)
{
    if (lhs.type != rhs.type)
    {
        return false;
    }

    switch (lhs.type)
    {
        case Value::Type::Bool: return lhs.b64 == rhs.b64;
        case Value::Type::UInt: return lhs.u64 == rhs.u64;
        case Value::Type::Double: return lhs.f64 == rhs.f64;
        case Value::Type::String: return lhs.str == rhs.str;
        default: assert(false && "Unhandled"); return false;
    }
}

static bool operator!=(const Value& lhs, const Value& rhs)
{
    if (lhs.type != rhs.type)
    {
        return true;
    }

    switch (lhs.type)
    {
        case Value::Type::Bool: return lhs.b64 != rhs.b64;
        case Value::Type::UInt: return lhs.u64 != rhs.u64;
        case Value::Type::Double: return lhs.f64 != rhs.f64;
        case Value::Type::String: return lhs.str != rhs.str;
        default: assert(false && "Unhandled"); return false;
    }
}

using NodeIndex = uint32_t;
static constexpr NodeIndex NO_NODE = 0;

struct Node
{
    enum class Type
    {
        Empty = 0,

        Literal,
        Attribute,
        Typeof,
        ToString,
        Format,

        Min,
        Max,

        Add,
        Subtract,
        Multiply,
        Divide,
        Remainder,
        Abs,
        Round,
        Floor,
        Ceil,

        Not,
        Equal,
        NotEqual,
        Lesser,
        LesserOrEqual,
        Greater,
        GreaterOrEqual,

        // Children should alternate between condition (booleans) and content (anything)
        Case,

        // Horizon-exclusive nodes
        Colorize,
        And,
        Or,

        // Mapbox-exclusive nodes (should not reach script serialisation)
        Array,
        HeatmapIntensity,
    };

    Node() = default;

    Type type = Type::Empty;

    hrz::InlinedVector<NodeIndex, 2> children;

    // Some nodes have no information to store but still require to be traversed to generate the
    // styling script. They do have children that are explored afterwards. An attribute with a child
    // literal for instance.

    union
    {
        Value literal{};
    };
};

// Represents a Horizon property inside a vector tiles representation. Several Mapbox properties can
// be merged into a single Horizon property since there isn't always a 1:1 mapping.
struct Property
{
    enum class Type
    {
        Invalid,
        Generic,
        Vec2,
        Vec3,
        ColorWithOpacity,
        ExtrudedVectorColor,
        SymbolAnchorAlignment,
        TextAlignment,
    };

    struct Generic
    {
        Value default_value;
        NodeIndex node = NO_NODE;

        Generic(Value default_value_) : default_value(default_value_) {}

        inline bool is_literal() const { return node == NO_NODE; }

        inline void visit_nodes(const std::function<void(NodeIndex root)>& cb) { cb(node); }
    };

    struct Vec2
    {
        Generic x;
        Generic y;

        Vec2(const Generic& x, const Generic& y) : x(x), y(y) {}

        inline bool is_literal() const { return x.is_literal() && y.is_literal(); }

        inline void visit_nodes(const std::function<void(NodeIndex root)>& cb)
        {
            x.visit_nodes(cb);
            y.visit_nodes(cb);
        }
    };

    struct Vec3
    {
        Generic x;
        Generic y;
        Generic z;

        Vec3(const Generic& x, const Generic& y, const Generic& z) : x(x), y(y), z(z) {}

        inline bool is_literal() const
        {
            return x.is_literal() && y.is_literal() && z.is_literal();
        }

        inline void visit_nodes(const std::function<void(NodeIndex root)>& cb)
        {
            x.visit_nodes(cb);
            y.visit_nodes(cb);
            z.visit_nodes(cb);
        }
    };

    struct ColorWithOpacity
    {
        Generic color;
        Generic opacity;

        ColorWithOpacity(const Generic& color, const Generic& opacity) :
            color(color), opacity(opacity)
        {
        }

        inline bool is_literal() const { return color.is_literal() && opacity.is_literal(); }

        inline void visit_nodes(const std::function<void(NodeIndex root)>& cb)
        {
            color.visit_nodes(cb);
            opacity.visit_nodes(cb);
        }

        inline hrz_proto::Color default_value_to_proto() const
        {
            hrz_proto::Color proto = hrz::convert_uint_to_proto_color(color.default_value.u64);
            proto.set_a(opacity.default_value.f64);
            return proto;
        }
    };

    // Extruded vector geometry colour property.
    struct ExtrudedVectorColor
    {
        Generic color;
        Generic opacity;
        Generic gradient;

        ExtrudedVectorColor(const Generic& color, const Generic& opacity, const Generic& gradient) :
            color(color), opacity(opacity), gradient(gradient)
        {
        }

        inline bool is_literal() const
        {
            return color.is_literal() && opacity.is_literal() && gradient.is_literal();
        }

        inline void visit_nodes(const std::function<void(NodeIndex root)>& cb)
        {
            color.visit_nodes(cb);
            opacity.visit_nodes(cb);
            gradient.visit_nodes(cb);
        }

        inline bool is_never_gradient() const
        {
            return gradient.is_literal() && gradient.default_value.b64 == false;
        }

        static void generate_separate_styling_names(
            std::string_view base_name,
            std::string* roof_name,
            std::string* upper_name,
            std::string* lower_name)
        {
            *roof_name = fmt::format("{}_roof", base_name);
            *upper_name = fmt::format("{}_upper", base_name);
            *lower_name = fmt::format("{}_lower", base_name);
        }
    };

    struct SymbolAnchorAlignment
    {
        Generic anchor;

        explicit SymbolAnchorAlignment(const Generic& anchor) : anchor(anchor) {}

        inline bool is_literal() const { return anchor.is_literal(); }

        inline void visit_nodes(const std::function<void(NodeIndex root)>& cb)
        {
            anchor.visit_nodes(cb);
        }

        static lm::dvec2 mapbox_anchor_to_horizon_alignment(std::string_view anchor);
    };

    struct TextAlignment
    {
        Generic text_justify;
        Generic text_anchor;

        explicit TextAlignment(const Generic& text_justify, const Generic& text_anchor) :
            text_justify(text_justify), text_anchor(text_anchor)
        {
        }

        inline bool is_literal() const
        {
            if (text_justify.is_literal())
            {
                if (text_justify.default_value.str == "auto")
                {
                    return text_anchor.is_literal();
                }

                return true;
            }

            return false;
        }

        inline void visit_nodes(const std::function<void(NodeIndex root)>& cb)
        {
            text_justify.visit_nodes(cb);
            text_anchor.visit_nodes(cb);
        }

        static hrz_proto::TextAlignment mapbox_text_justify_to_horizon_alignment(
            std::string_view text_justify);
    };

    Property() : type(Type::Invalid) {}

    Property(std::string_view name, const Generic& value) :
        styling_name(name), type(Type::Generic), generic(value)
    {
    }

    Property(std::string_view name, const Vec2& value) :
        styling_name(name), type(Type::Vec2), vec2(value)
    {
    }

    Property(std::string_view name, const Vec3& value) :
        styling_name(name), type(Type::Vec3), vec3(value)
    {
    }

    Property(std::string_view name, const ColorWithOpacity& value) :
        styling_name(name), type(Type::ColorWithOpacity), color_with_opacity(value)
    {
    }

    Property(std::string_view name, const ExtrudedVectorColor& value) :
        styling_name(name), type(Type::ExtrudedVectorColor), extruded_color(value)
    {
    }

    Property(std::string_view name, const SymbolAnchorAlignment& value) :
        styling_name(name), type(Type::SymbolAnchorAlignment), symbol_anchor_alignment(value)
    {
    }

    Property(std::string_view name, const TextAlignment& value) :
        styling_name(name), type(Type::TextAlignment), text_alignment(value)
    {
    }

    bool is_literal() const
    {
        switch (type)
        {
            case Type::Generic: return generic.is_literal();
            case Type::Vec2: return vec2.is_literal();
            case Type::Vec3: return vec3.is_literal();
            case Type::ColorWithOpacity: return color_with_opacity.is_literal();
            case Type::ExtrudedVectorColor: return extruded_color.is_literal();
            case Type::SymbolAnchorAlignment: return symbol_anchor_alignment.is_literal();
            case Type::TextAlignment: return text_alignment.is_literal();
            default: assert(false && "Unhandled case"); return true;
        }
    }

    void visit_nodes(const std::function<void(NodeIndex root)>& cb)
    {
        switch (type)
        {
            case Type::Generic: generic.visit_nodes(cb); break;
            case Type::Vec2: vec2.visit_nodes(cb); break;
            case Type::Vec3: vec3.visit_nodes(cb); break;
            case Type::ColorWithOpacity: color_with_opacity.visit_nodes(cb); break;
            case Type::ExtrudedVectorColor: extruded_color.visit_nodes(cb); break;
            case Type::SymbolAnchorAlignment: symbol_anchor_alignment.visit_nodes(cb); break;
            case Type::TextAlignment: text_alignment.visit_nodes(cb); break;
            default: assert(false && "Unhandled case");
        }
    }

    std::string styling_name;
    Type type;

    union
    {
        // Used for all simple properties that have a 1:1 mapping between Mapbox and Horizon and
        // don't require extra work during the translation.
        // This applies for instance for the `fill-extrusion-height` Mapbox property that maps to
        // Horizon's extruded geometries height.
        Generic generic;
        Vec2 vec2;
        Vec3 vec3;

        // All the following don't have a simple 1:1 mapping and require extra operation during the
        // the generation of the representation's styling script.

        // Some Mapbox layers have both a "color" and "opacity" property. Both must be mapped
        // to the same color property in Horizon.
        ColorWithOpacity color_with_opacity;

        // The 'fill-extrusion-color', 'fill-extrusion-opacity', 'fill-extrusion-vertical-gradient'
        // Mapbox properties are all mapped to the single property for the extruded geometries
        // colour.
        ExtrudedVectorColor extruded_color;

        // The `icon-anchor` and `text-anchor` Mapbox string properties are mapped to the
        // symbol anchors `element_alignment` vec2 property.
        SymbolAnchorAlignment symbol_anchor_alignment;

        // The `text-justify` is mapped to the text symbol element `alignment` property.
        // When the value of `text-justify` is "auto", the value of `text_anchor` must be
        // taking into account as well.
        TextAlignment text_alignment;
    };
};

void generate_representations_script(
    std::span<const Node> nodes,
    std::span<const Property> properties,
    std::span<const std::string_view> representation_names,
    uint32_t first_representation_id,
    NodeIndex filter_node,
    std::string& script);

void assign_default_value(const Property::Generic& prp, hrz_proto::FloatProperty* proto);

void assign_default_value(const Property::Generic& prp, hrz_proto::IntProperty* proto);

void assign_default_value(const Property::Generic& prp, hrz_proto::StringProperty* proto);

void assign_default_value(const Property::Vec2& prp, hrz_proto::Vec2fProperty* proto);
void assign_default_value(
    const Property::SymbolAnchorAlignment& prp,
    hrz_proto::Vec2fProperty* proto);

void assign_default_value(const Property::Vec2& prp, hrz_proto::Vec3fProperty* proto);
void assign_default_value(const Property::Vec3& prp, hrz_proto::Vec3fProperty* proto);

void assign_default_value(const Property::Generic& prp, hrz_proto::ColorProperty* proto);
void assign_default_value(const Property::ColorWithOpacity& prp, hrz_proto::ColorProperty* proto);

void assign_default_value(
    const Property::TextAlignment& prp,
    hrz_proto::TextAlignmentProperty* proto);

// https://docs.mapbox.com/style-spec/reference/types/#color
bool parse_mapbox_color_string(std::string_view input, uint32_t* color);

} // namespace hrz_mapbox
