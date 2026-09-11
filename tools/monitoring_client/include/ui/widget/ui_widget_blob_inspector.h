// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "data.h"
#include "hrz/fnd/flat_hash_set.h"
#include "ui/common/ui_common_metadata.h"
#include "ui/common/ui_common_sorted_table.h"
#include "ui/ui_context.h"
#include "ui/ui_helpers.h"
#include "ui/ui_view.h"
#include "userdata.h"

namespace ui::widget
{

class BlobInspector
{
public:
    BlobInspector();

    void draw(
        const helpers::Rect& area,
        const data::Database&,
        userdata::Userdata&,
        context::ActionBus&);

    void set_blob_snapshot_index(size_t index);
    void on_database_clear();

private:
    size_t _selected_snapshot = 0;
    std::optional<size_t> _context_menu_allocated_blob_index;

    std::unique_ptr<sorted_table::SortedTable<data::Blob>> _sorted_table;
    hrz::flat_hash_set<std::string> _metadata_columns;
    bool _reset_metadata_columns = true;

    ui::view::View _view;
    bool _lock_view = false;

    struct Filter
    {
        std::string system;
        std::string layer;

        size_t size_min = 0;
        size_t size_max = 0;

        size_t max_size_max = 0;

        std::vector<metadata::Filter> metadata;
    };

    Filter _filter;
    bool _reset_filter = true;

    bool _pass_filter(const data::Blob&) const;
    std::string _save_filter_to_json_string() const;
    bool _load_filter_from_json_string(std::string_view json);
};

} // namespace ui::widget
