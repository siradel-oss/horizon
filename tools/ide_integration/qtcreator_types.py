# SPDX-FileCopyrightText: Copyright 2020 Siradel
# SPDX-License-Identifier: MIT

# References:
# https://doc.qt.io/qtcreator/creator-debugging-helpers.html
# https://stackoverflow.com/questions/34354573/how-to-write-a-debugging-helper-for-qtcreator
# https://discourse.urho3d.io/t/qtcreator-debugging-helper/2849/3

from dumper import Children, SubItem, UnnamedSubItem, DumperBase
from pprint import pprint

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


def generic_vector(
    d,
    value,
    names,
    value_transform=no_transform,
    bounds_fmt="({})",
    value_fmt="{value}",
):
    values = [
        value_fmt.format(name=n, value=value_transform(value[n].value())) for n in names
    ]
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
    if value_type.name == "float" or value_type.name == "double":
        value_transform = fixed_rounding
    elif value_type.name == "bool":
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


def generic_bbox(
    d, value, names, value_transform=no_transform, bounds_fmt="({})", value_fmt="{}"
):
    min_values = [
        value_fmt.format(value_transform(value["min"][n].value())) for n in names
    ]
    max_values = [
        value_fmt.format(value_transform(value["max"][n].value())) for n in names
    ]
    d.putValue(
        bounds_fmt.format(", ".join(min_values))
        + " -> "
        + bounds_fmt.format(", ".join(max_values))
    )
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
# nonstd
#


def qdump__nonstd__optional_lite__optional(d, value):
    if value["has_value_"].integer():
        value_type = value.type.templateArgument(0)
        storage = value["contained"]["data"]["__data"]
        # contained_value = storage.cast(value_type).value()
        d.putValue("<1 item>")
        d.putNumChild(1)
        if d.isExpanded():
            d.putArrayData(storage.address(), 1, value_type)
    else:
        d.putValue("<empty>")


#
# Abseil
#


def qdump__absl__lts_20211102__optional(d, value):
    if value["engaged_"].integer():
        value_type = value.type.templateArgument(0)
        storage = value["data_"]
        d.putValue("<1 item>")
        d.putNumChild(1)
        if d.isExpanded():
            d.putArrayData(storage.address(), 1, value_type)
    else:
        d.putValue("<empty>")


def qdump__absl__lts_20211102__variant(d, value):
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


def qdump__absl__lts_20211102__string_view(d, value):
    d.putSimpleCharArray(value["ptr_"], value["length_"])
    d.putPlainChildren(value)


def qdump__absl__lts_20211102__flat_hash_map(d, value):
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


def qdump__absl__lts_20211102__flat_hash_set(d, value):
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


def qdump__absl__lts_20211102__InlinedVector(d, value):
    try:
        value_type = value.type.templateArgument(0)
        storage = value["storage_"]
        metadata = storage["metadata_"]["value"].integer()
        size = metadata >> 1
        allocated = (metadata & 1) == 1
        d.putItemCount(size)
        if d.isExpanded():
            if allocated:
                addr = storage["data_"]["allocated"]["allocated_data"].pointer()
            else:
                addr = storage["data_"]["inlined"]["inlined_data"].address()
            d.putArrayData(addr, size, value_type)
    except Exception as e:
        d.putValue(f"<InlinedVector error: {str(e)}>")
        d.putPlainChildren(value)


def qdump__absl__lts_20240722__optional(d, value):
    if value["engaged_"].integer():
        value_type = value.type.templateArgument(0)
        storage = value["data_"]
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


# Abseil 20250814 SwissTable (raw_hash_set) memory layout, as used by
# flat_hash_map / flat_hash_set:
#
#   raw_hash_set::settings_  is a CompressedTuple whose first element is a
#   CommonFields object, reachable as settings_["value"]. CommonFields holds:
#     - capacity_        (size_t)      total number of slots
#     - size_            (HashtableSize { uint64_t data_ })
#                        actual size = data_ >> kSizeShift, kSizeShift == 17
#     - heap_or_soo_     (union HeapOrSoo)
#          .heap.control.p     ctrl_t*  control bytes (occupied when byte >= 0)
#          .heap.slot_array.p  void*    start of the slot array
#          .soo_data[16]                inline storage for the single SOO element
#
# When capacity <= 1 the table is "small": it holds at most one element, stored
# either inline in soo_data (when small-object-optimization is enabled, i.e. the
# slot is small enough) or in a one-slot heap allocation (slot_array) otherwise.
# Otherwise the table is a regular SwissTable and we iterate the control bytes.

# HashtableSize::kSizeShift == 64 - PerTableSeed::kBitCount(16) - 1 == 47, and
# size = data_ >> (64 - kSizeBitCount) == data_ >> 17.
_ABSL_SIZE_SHIFT = 17

# sizeof(HeapOrSoo) / alignof(HeapOrSoo): SOO storage is two pointers wide.
_ABSL_SOO_SLOT_SIZE = 16
_ABSL_SOO_SLOT_ALIGN = 8


def _absl__lts_20250814_swiss_common(value):
    # CommonFields is settings_.get<0>(), stored as the CompressedTuple's
    # "value" member.
    common = value["settings_"]["value"]
    capacity = common["capacity_"].integer()
    size = common["size_"]["data_"].integer() >> _ABSL_SIZE_SHIFT
    return common, capacity, size


def _absl__lts_20250814_put_slot(d, index, addr, element_type, is_map):
    slot = d.createValue(addr, element_type)
    if is_map:
        d.putPairItem(index, (slot["first"], slot["second"]))
    else:
        d.putSubItem(index, slot)


def _absl__lts_20250814_swiss_dump(d, value, element_type, is_map):
    common, capacity, size = _absl__lts_20250814_swiss_common(value)
    d.putItemCount(size)
    if not d.isExpanded() or size <= 0:
        return

    slot_size = element_type.size()
    heap_or_soo = common["heap_or_soo_"]

    # Small table: at most one element (size is 1 here since size > 0).
    if capacity <= 1:
        soo_enabled = (
            slot_size <= _ABSL_SOO_SLOT_SIZE
            and element_type.alignment() <= _ABSL_SOO_SLOT_ALIGN
        )
        if soo_enabled:
            base = heap_or_soo["soo_data"].address()
        else:
            base = heap_or_soo["heap"]["slot_array"]["p"].pointer()
        with Children(d):
            _absl__lts_20250814_put_slot(d, 0, base, element_type, is_map)
        return

    # Regular SwissTable: walk the control bytes, emit occupied slots.
    heap = heap_or_soo["heap"]
    control = heap["control"]["p"]
    slot_base = heap["slot_array"]["p"].pointer()
    with Children(d):
        item_index = 0
        i = 0
        while i < capacity and item_index < size and i < 1000000:
            # Occupied control bytes are the 7-bit H2 hash (0x00..0x7F); empty
            # (0x80), deleted (0xFE) and sentinel (0xFF) all have the high bit
            # set. Mask to a byte so the check is robust to signed/unsigned reads.
            if (control[i].integer() & 0xFF) < 0x80:
                _absl__lts_20250814_put_slot(
                    d, item_index, slot_base + i * slot_size, element_type, is_map
                )
                item_index += 1
            i += 1


def qdump__absl__lts_20250814__flat_hash_set(d, value):
    try:
        element_type = value.type.templateArgument(0)
        _absl__lts_20250814_swiss_dump(d, value, element_type, is_map=False)
    except Exception as e:
        d.putValue(f"<flat_hash_set error: {str(e)}>")
        d.putPlainChildren(value)


def qdump__absl__lts_20250814__flat_hash_map(d, value):
    try:
        key_type = value.type.templateArgument(0)
        mapped_type = value.type.templateArgument(1)
        # The slot's value_type is std::pair<const K, V>; the non-const
        # std::pair<K, V> is layout-compatible (same size/alignment) and avoids
        # having to guess how the debugger spells the const qualifier.
        pair_name = "std::pair<%s, %s>" % (key_type.name, mapped_type.name)
        element_type = d.lookupType(pair_name)
        if element_type is None:
            d.putValue(f"<flat_hash_map: cannot resolve '{pair_name}'>")
            d.putPlainChildren(value)
            return
        _absl__lts_20250814_swiss_dump(d, value, element_type, is_map=True)
    except Exception as e:
        d.putValue(f"<flat_hash_map error: {str(e)}>")
        d.putPlainChildren(value)


def qdump__absl__lts_20250814__InlinedVector(d, value):
    # Storage layout (unchanged since 20211102):
    #   storage_.metadata_  CompressedTuple<Allocator, SizeType>; the SizeType is
    #                       reachable as metadata_["value"] and packs
    #                       (size << 1) | is_allocated.
    #   storage_.data_      union { Allocated { data, capacity }; Inlined { bytes }; }
    try:
        value_type = value.type.templateArgument(0)
        storage = value["storage_"]
        metadata = storage["metadata_"]["value"].integer()
        size = metadata >> 1
        allocated = (metadata & 1) == 1
        d.putItemCount(size)
        if d.isExpanded():
            if allocated:
                addr = storage["data_"]["allocated"]["allocated_data"].pointer()
            else:
                addr = storage["data_"]["inlined"]["inlined_data"].address()
            d.putArrayData(addr, size, value_type)
    except Exception as e:
        d.putValue(f"<InlinedVector error: {str(e)}>")
        d.putPlainChildren(value)


#
# Horizon
#


def qdump__hrz__GeoPosition2(d, value):
    generic_vector(d, value, ["lat", "lon"], fixed_rounding, "{}", "{name}: {value}")


def qdump__hrz__GeoPosition3(d, value):
    generic_vector(
        d, value, ["lat", "lon", "alt"], fixed_rounding, "{}", "{name}: {value}"
    )


def qdump__hrz__GeoBounds(d, value):
    generic_vector(
        d,
        value,
        ["west", "south", "east", "north"],
        fixed_rounding,
        "{}",
        "{name}: {value}",
    )


def qdump__hrz__GeoVolumeBounds(d, value):
    generic_vector(
        d,
        value,
        ["west", "south", "east", "north", "min_height", "max_height"],
        fixed_rounding,
        "{}",
        "{name}: {value}",
    )


def qdump__hrz__TileCoords(d, value):
    generic_vector(d, value, ["lod", "x", "y"], no_transform, "{}", "{name}: {value}")
