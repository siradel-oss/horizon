#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <span>
#include <string_view>
#include <string>

namespace HrzProtocol
{
{% for type in path_types %}
{% if not type.is_primitive and not type.is_enum %}
class {{ type.name }};
{% endif %}
{% endfor %}
} // namespace HrzProtocol

namespace hrz::scene_model
{

{% for type in path_types %}
{% if not type.is_primitive and not type.is_enum %}

/**
 * Retrieves a serialized message from message obj at the given path.
 */
std::string get_message_part_raw(
    const {{ type.full_name|to_cpp_qualified_name }}& obj,
    std::span<const uint32_t> path);

/**
 * Sets the serialized message raw to message obj at
 * the given path.
 */
void set_message_part_raw(
    {{ type.full_name|to_cpp_qualified_name }}& obj,
    std::span<const uint32_t> path,
    std::string_view raw);

/**
 * Counts the number of elements at a given path.
 * If the path does not point to a valid repeated field, returns 0.
 */
uint32_t count_message_part(
    const {{ type.full_name|to_cpp_qualified_name }}& obj,
    std::span<const uint32_t> path);

/**
 * Adds the serialized message raw to the repeated fields in obj pointed
 * to by the given path.
 * Returns the new number of elements in the repeated field.
 * Returns 0 if the path did not point to a repeated field.
 */
uint32_t add_message_part_raw(
    {{ type.full_name|to_cpp_qualified_name }}& obj,
    std::span<const uint32_t> path,
    std::string_view raw);

/**
 * Removes an element from a repeated field pointed by path from obj.
 * Returns the new number of elements in the repeated field.
 * Returns 0 if the path did not point to an element of a repeated field.
 * Returns the repeated field size if the index was out of bounds.
 */
uint32_t remove_message_part(
    {{ type.full_name|to_cpp_qualified_name }}& obj,
    std::span<const uint32_t> path);

{% endif %}
{% endfor %}

/**
 * Retrieves the message output of type Out from message obj of type In
 * at the given path.
 * Returns whether the message could be retrieved or not.
 * Use the raw version if you want serialized data.
 */
template<typename In, typename Out>
bool get_message_part(
    const In& obj,
    std::span<const uint32_t> path,
    Out& output)
{
    std::string raw = std::move(get_message_part_raw(obj, path));
    return output.ParseFromString(raw);
}

/**
 * Sets the message part of type Part to message obj of type In at
 * the given path.
 * Use the raw version if you have serialized data.
 */
template<typename In, typename Part>
void set_message_part(
    In& obj,
    std::span<const uint32_t> path,
    const Part& part)
{
    std::string raw = part.SerializeAsString();
    set_message_part_raw(obj, path, raw);
}

/**
 * Adds the message part of type Part to the repeated fields in obj pointed
 * to by the given path.
 * Returns the new number of elements in the repeated field.
 * Returns 0 if the path did not point to a repeated field.
 * Use the raw version if you have serialized data.
 */
template<typename In, typename Part>
uint32_t add_message_part(
    In& obj,
    std::span<const uint32_t> path,
    const Part& part)
{
    std::string raw = part.SerializeAsString();
    return add_message_part_raw(obj, path, raw);
}

}
