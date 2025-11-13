#pragma once

#include <span>
#include <stdint.h>

namespace hrz
{
/**
 * A selection system manages what parts of the scene are selected. Each "part"
 * of the scene is something that is identified by a layer id and a feature id.
 * It is used to retrieve information like what parts of the scene were select
 * or deselected since last frame so that those changes can be incremental.
 */
struct SelectionSystem;

namespace selection
{
SelectionSystem* create();
void destroy(SelectionSystem*);

void select(SelectionSystem*, uint64_t layer_id, uint64_t object_id);
void deselect(SelectionSystem*, uint64_t layer_id, uint64_t object_id);
void deselect_all(SelectionSystem*);
void finish_frame(SelectionSystem*);

bool is_selected(const SelectionSystem*, uint64_t layer_id, uint64_t object_id);
bool has_changed_since_last_frame(const SelectionSystem*);
bool has_changed_since_last_frame(const SelectionSystem*, uint64_t layer_id);
size_t selected_objects_count(const SelectionSystem*);
size_t selected_objects_count(const SelectionSystem*, uint64_t layer_id);

/**
 * out_object_ids's size must be >= the size returned by selected_objects_count.
 * For convenience the number of objects written to out_objects_ids is also returned.
 */
size_t get_selected_objects(
    const SelectionSystem*,
    uint64_t layer_id,
    std::span<uint64_t> out_object_ids);

} // namespace selection
} // namespace hrz
