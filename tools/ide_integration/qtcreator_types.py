# References:
# https://doc.qt.io/qtcreator/creator-debugging-helpers.html
# https://stackoverflow.com/questions/34354573/how-to-write-a-debugging-helper-for-qtcreator
# https://discourse.urho3d.io/t/qtcreator-debugging-helper/2849/3

from dumper import *

#
#   lm
#

def no_transform(v):
    return v

def fixed_rounding(v):
    # Sometime the value is a GDB native value, and in this case calling .value(),
    # or even .floatingPoint(), returns a string. So we have to parse it before the
    # number can be rounded.
    v = float(v)
    if abs(v) < 1:
        return "{:#.4g}".format(v)
    return round(v, 4)

def to_boolean(v):
    if v == 0:
        return "false"
    return "true"

def generic_vector(d, value, names, value_transform=no_transform, bounds_fmt="({})", value_fmt="{value}"):
    values = [value_fmt.format(name=n, value=value_transform(value[n].value())) for n in names]
    d.putValue(bounds_fmt.format(", ".join(values)))
    d.putNumChild(len(names))
    if d.isExpanded():
        with Children(d):
            for n in names:
                d.putSubItem(n, value[n])

def qdump__lm__Vector(d, value):
    value_type = value.type.templateArgument(0)
    size = value.type.templateArgument(1)

    value_transform = no_transform
    if (value_type.name == "float" or value_type.name == "double"):
        value_transform = fixed_rounding
    elif (value_type.name == "bool"):
        value_transform = to_boolean

    names = ["", "x", "xy", "xyz", "xyzw"][size]

    generic_vector(d, value, names, value_transform)

def qdump__lm__vec2(d, value):
    generic_vector(d, value, "xy", fixed_rounding)

def qdump__lm__vec3(d, value):
    generic_vector(d, value, "xyz", fixed_rounding)

def qdump__lm__vec4(d, value):
    generic_vector(d, value, "xyzw", fixed_rounding)

def qdump__lm__dvec2(d, value):
    generic_vector(d, value, "xy", fixed_rounding)

def qdump__lm__dvec3(d, value):
    generic_vector(d, value, "xyz", fixed_rounding)

def qdump__lm__dvec4(d, value):
    generic_vector(d, value, "xyzw", fixed_rounding)

def qdump__lm__ivec2(d, value):
    generic_vector(d, value, "xy")

def qdump__lm__ivec3(d, value):
    generic_vector(d, value, "xyz")

def qdump__lm__ivec4(d, value):
    generic_vector(d, value, "xyzw")

def qdump__lm__uvec2(d, value):
    generic_vector(d, value, "xy")

def qdump__lm__uvec3(d, value):
    generic_vector(d, value, "xyz")

def qdump__lm__uvec4(d, value):
    generic_vector(d, value, "xyzw")

def qdump__lm__ubvec2(d, value):
    generic_vector(d, value, "xy")

def qdump__lm__ubvec3(d, value):
    generic_vector(d, value, "xyz")

def qdump__lm__ubvec4(d, value):
    generic_vector(d, value, "xyzw")

def qdump__lm__ibvec2(d, value):
    generic_vector(d, value, "xy")

def qdump__lm__ibvec3(d, value):
    generic_vector(d, value, "xyz")

def qdump__lm__ibvec4(d, value):
    generic_vector(d, value, "xyzw")

def qdump__lm__bvec2(d, value):
    generic_vector(d, value, "xy", to_boolean)

def qdump__lm__bvec3(d, value):
    generic_vector(d, value, "xyz", to_boolean)

def qdump__lm__bvec4(d, value):
    generic_vector(d, value, "xyzw", to_boolean)

def generic_bbox(d, value, names, value_transform=no_transform, bounds_fmt="({})", value_fmt="{}"):
    min_values = [value_fmt.format(value_transform(value["min"][n].value())) for n in names]
    max_values = [value_fmt.format(value_transform(value["max"][n].value())) for n in names]
    d.putValue(bounds_fmt.format(", ".join(min_values)) + " -> " + bounds_fmt.format(", ".join(max_values)))
    d.putNumChild(2)
    if d.isExpanded():
        with Children(d):
            d.putSubItem(0, value["min"])
            d.putSubItem(1, value["max"])

def qdump__lm__bbox2(d, value):
    generic_bbox(d, value, "xy", fixed_rounding)

def qdump__lm__bbox3(d, value):
    generic_bbox(d, value, "xyz", fixed_rounding)

def qdump__lm__dbbox2(d, value):
    generic_bbox(d, value, "xy", fixed_rounding)

def qdump__lm__dbbox3(d, value):
    generic_bbox(d, value, "xyz", fixed_rounding)

def qdump__lm__ibbox2(d, value):
    generic_bbox(d, value, "xy")

def qdump__lm__ibbox3(d, value):
    generic_bbox(d, value, "xyz")

#
#   gsl
#

def qdump__gsl__span(d, value):
    value_type = value.type.templateArgument(0)
    first_addr = int(value["first_"].value())
    last_addr = int(value["last_"].value())
    item_count = (last_addr - first_addr) // value_type.size()
    d.putItemCount(item_count)
    if d.isExpanded():
        d.putArrayData(first_addr, item_count, value_type)

def qdump__gsl__byte(d, value):
    d.putValue(value.integer())

#
# Abseil
#

def qdump__absl__lts_20240722__optional(d, value):
    if value["engaged_"].integer():
        value_type = value.type.templateArgument(0)
        storage = value["data_"]
        #contained_value = storage.cast(value_type).value()
        d.putValue("<1 item>")
        d.putNumChild(1)
        if d.isExpanded():
            d.putArrayData(storage.address(), 1, value_type)
    else:
        d.putValue("<empty>")

def qdump__absl__lts_20240722__variant(d, value):
    index = value["index_"].integer()
    if index >= 0:
        value_type = value.type.templateArgument(index)
        storage = value["state_"]["head"]
        d.putValue("<variant index " + str(index) + ">")
        d.putNumChild(1)
        if d.isExpanded():
            d.putArrayData(storage.address(), 1, value_type)
    else:
        d.putValue("<valueless by exception>")

def qdump__absl__lts_20240722__string_view(d, value):
    d.putSimpleCharArray(value["ptr_"], value["length_"])
    d.putPlainChildren(value)

def qdump__absl__lts_20240722__flat_hash_map(d, value):
    size = value["size_"].integer()
    capacity = value["capacity_"].integer()
    d.putItemCount(size)
    if d.isExpanded():
        with Children(d):
            i = 0
            while i < capacity and i < 10000:
                if value["ctrl_"][i].integer() >= 0:
                    key_value = value["slots_"][i]["value"]
                    d.putPairItem(i, (key_value["first"], key_value["second"]))
                i += 1

def qdump__absl__lts_20240722__flat_hash_set(d, value):
    size = value["size_"].integer()
    capacity = value["capacity_"].integer()
    d.putItemCount(size)
    if d.isExpanded():
        with Children(d):
            i = 0
            while i < capacity and i < 10000:
                if value["ctrl_"][i].integer() >= 0:
                    d.putSubItem(0, value["slots_"][i])
                i += 1

#
# Horizon
#

def qdump__hrz__GeoPosition2(d, value):
    generic_vector(d, value, ["lat", "lon"], fixed_rounding, "{}", "{name}: {value}")

def qdump__hrz__GeoPosition3(d, value):
    generic_vector(d, value, ["lat", "lon", "alt"], fixed_rounding, "{}", "{name}: {value}")

def qdump__hrz__GeoBounds(d, value):
    generic_vector(d, value, ["west", "south", "east", "north"], fixed_rounding, "{}", "{name}: {value}")

def qdump__hrz__GeoVolumeBounds(d, value):
    generic_vector(d, value, ["west", "south", "east", "north", "min_height", "max_height"], fixed_rounding, "{}", "{name}: {value}")

def qdump__hrz__TileCoords(d, value):
    generic_vector(d, value, ["lod", "x", "y"], no_transform, "{}", "{name}: {value}")
