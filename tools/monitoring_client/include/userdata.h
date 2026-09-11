// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <rapidjson/fwd.h>

#include <list>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace userdata
{

using ParsingErrorFlags = int;

enum ParsingError
{
    ParsingError_None = 0,
    ParsingError_InvalidData = 1,
    ParsingError_MissingData = 2
};

struct GpuResourceFilterPreset
{
    std::string json;
};

struct BlobFilterPreset
{
    std::string json;
};

struct WindowLayoutPreset
{
    struct Window
    {
        size_t type;
        std::string name;
        size_t key;
    };

    std::string imgui_ini_data;
    std::vector<Window> windows;
};

template<typename Preset>
using PresetMap = std::map<std::string, Preset>;

class RecentElementSystem
{
public:
    void push_element(const char* element);
    void remove_element(const char* element);

    void clear()
    {
        _elements.clear();
        _views.clear();
    }

    std::span<const char* const> get_elements() const { return _views; };

    RecentElementSystem(size_t max_elements = 12);

private:
    std::list<std::string> _elements;
    std::vector<const char*> _views;
    size_t _max_elements;

    void _update_view_vector();
};

class Userdata
{
public:
    bool save_to_disk() const;
    ParsingErrorFlags load_from_disk();

    PresetMap<GpuResourceFilterPreset> gpu_resource_filter_presets;
    PresetMap<BlobFilterPreset> blob_filter_presets;
    PresetMap<WindowLayoutPreset> window_layout_presets;
    RecentElementSystem recently_opened_paths;
    size_t style_index;
    std::string layout_name;

private:
    std::string _to_json_string() const;
    ParsingErrorFlags _load_from_json_string(const std::string& json);
};

} // namespace userdata
