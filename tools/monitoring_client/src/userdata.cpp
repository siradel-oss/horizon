#include "userdata.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>

#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <errno.h>
#    include <sys/stat.h>
#    include <sys/types.h>
#endif

namespace
{
using namespace userdata;

std::string _userdata_dir()
{
    std::string path;

#ifdef _WIN32
    path = std::getenv("APPDATA");
    path += "\\Horizon Monitoring";
#else
    path = std::getenv("HOME");
    path += "/.config/hrz_monitoring";
#endif

    return path;
}

std::string _userdata_file()
{
#ifdef _WIN32
    return _userdata_dir() + "\\userdata.json";
#else
    return _userdata_dir() + "/userdata.json";
#endif
}

bool _create_dir_if_inexistent(const char* path)
{
#ifdef _WIN32
    bool success = CreateDirectory(path, nullptr);
    return success || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    int result = mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO);
    return (result >= 0) || (result < 0 && errno == EEXIST);
#endif
}

template<typename T>
void _json_write_primitive_member(
    const char* name,
    T value,
    rapidjson::Value& object,
    rapidjson::Document& doc)
{
    assert(object.IsObject());

    rapidjson::Value member;
    member.Set(value, doc.GetAllocator());

    object.AddMember(rapidjson::StringRef(name), member, doc.GetAllocator());
}

template<typename T>
ParsingErrorFlags _json_read_primitive_member(
    const char* name,
    T& value,
    const rapidjson::Value& object)
{
    if (!object.IsObject()) return false;

    auto it = object.FindMember(name);

    if (it == object.MemberEnd()) return ParsingError_MissingData;

    value = it->value.Get<T>();
    return ParsingError_None;
}

void _json_write_string_member(
    const char* name,
    const std::string& string,
    rapidjson::Value& object,
    rapidjson::Document& doc)
{
    assert(object.IsObject());

    rapidjson::Value member;
    member.SetString(string.c_str(), doc.GetAllocator());

    object.AddMember(rapidjson::StringRef(name), member, doc.GetAllocator());
}

ParsingErrorFlags _json_read_string_member(
    const char* name,
    std::string& string,
    const rapidjson::Value& object)
{
    if (!object.IsObject()) return false;

    auto it = object.FindMember(name);

    if (it == object.MemberEnd())
        return ParsingError_MissingData;
    else if (!it->value.IsString())
        return ParsingError_InvalidData;

    string = it->value.GetString();
    return ParsingError_None;
}

template<typename T>
void _json_write_primitive_vector_member(
    const char* name,
    const std::vector<T>& vector,
    rapidjson::Value& object,
    rapidjson::Document& doc)
{
    assert(object.IsObject());

    rapidjson::Value member;
    member.SetArray();

    for (auto element : vector)
    {
        member.PushBack(element, doc.GetAllocator());
    }

    object.AddMember(rapidjson::StringRef(name), member, doc.GetAllocator());
}

template<typename T>
ParsingErrorFlags _json_read_primitive_vector_member(
    const char* name,
    std::vector<T>& vector,
    const rapidjson::Value& object)
{
    if (!object.IsObject()) return ParsingError_InvalidData;

    auto it = object.FindMember(name);

    if (it == object.MemberEnd())
        return ParsingError_MissingData;
    else if (!it->value.IsArray())
        return ParsingError_InvalidData;

    vector.clear();

    auto& array = it->value;
    for (rapidjson::SizeType i = 0; i < array.Size(); ++i)
    {
        auto& value = array[i];

        if (!value.Is<T>()) return ParsingError_InvalidData;

        vector.push_back(value.Get<T>());
    }

    return ParsingError_None;
}

template<typename T>
void _json_write_preset_object(
    const std::string& name,
    const T& preset,
    rapidjson::Value& value,
    rapidjson::Document& doc);
template<typename T>
ParsingErrorFlags _json_read_preset_object(
    std::string& name,
    T& preset,
    const rapidjson::Value& value);

template<>
void _json_write_preset_object<GpuResourceFilterPreset>(
    const std::string& name,
    const GpuResourceFilterPreset& preset,
    rapidjson::Value& value,
    rapidjson::Document& doc)
{
    value.SetObject();
    _json_write_string_member("name", name, value, doc);
    _json_write_string_member("json", preset.json, value, doc);
}

template<>
ParsingErrorFlags _json_read_preset_object<GpuResourceFilterPreset>(
    std::string& name,
    GpuResourceFilterPreset& preset,
    const rapidjson::Value& value)
{
    if (!value.IsObject()) return ParsingError_InvalidData;

    int error = ParsingError_None;
    error &= _json_read_string_member("name", name, value);
    error &= _json_read_string_member("json", preset.json, value);

    return error;
}

template<>
void _json_write_preset_object<BlobFilterPreset>(
    const std::string& name,
    const BlobFilterPreset& preset,
    rapidjson::Value& value,
    rapidjson::Document& doc)
{
    value.SetObject();
    _json_write_string_member("name", name, value, doc);
    _json_write_string_member("json", preset.json, value, doc);
}

template<>
ParsingErrorFlags _json_read_preset_object<BlobFilterPreset>(
    std::string& name,
    BlobFilterPreset& preset,
    const rapidjson::Value& value)
{
    if (!value.IsObject()) return ParsingError_InvalidData;

    int error = ParsingError_None;
    error &= _json_read_string_member("name", name, value);
    error &= _json_read_string_member("json", preset.json, value);

    return error;
}

template<>
void _json_write_preset_object<WindowLayoutPreset>(
    const std::string& name,
    const WindowLayoutPreset& preset,
    rapidjson::Value& value,
    rapidjson::Document& doc)
{
    value.SetObject();
    _json_write_string_member("name", name, value, doc);
    _json_write_string_member("ini", preset.imgui_ini_data, value, doc);

    {
        rapidjson::Value window_array;
        window_array.SetArray();

        for (const auto& window : preset.windows)
        {
            rapidjson::Value window_object;
            window_object.SetObject();

            _json_write_primitive_member("type", window.type, window_object, doc);
            _json_write_string_member("name", window.name, window_object, doc);
            _json_write_primitive_member("key", window.key, window_object, doc);

            window_array.PushBack(window_object, doc.GetAllocator());
        }

        value.AddMember(rapidjson::StringRef("windows"), window_array, doc.GetAllocator());
    }
}

template<>
ParsingErrorFlags _json_read_preset_object<WindowLayoutPreset>(
    std::string& name,
    WindowLayoutPreset& preset,
    const rapidjson::Value& value)
{
    if (!value.IsObject()) return false;

    int error = 0;
    error &= _json_read_string_member("name", name, value);
    error &= _json_read_string_member("ini", preset.imgui_ini_data, value);

    {
        auto it = value.FindMember("windows");

        if (it == value.MemberEnd())
            return ParsingError_MissingData;
        else if (!it->value.IsArray())
            return ParsingError_InvalidData;

        preset.windows.clear();

        auto& array = it->value;
        for (rapidjson::SizeType i = 0; i < array.Size(); ++i)
        {
            auto& value = array[i];

            if (!value.IsObject()) return ParsingError_InvalidData;

            userdata::WindowLayoutPreset::Window window;

            error &= _json_read_primitive_member("type", window.type, value);
            error &= _json_read_string_member("name", window.name, value);
            error &= _json_read_primitive_member("key", window.key, value);

            preset.windows.push_back(window);
        }
    }

    return error;
}

template<typename Preset>
void _json_write_presets_map_member(
    const char* name,
    const PresetMap<Preset>& map,
    rapidjson::Value& value,
    rapidjson::Document& doc)
{
    assert(value.IsObject());

    rapidjson::Value array;
    array.SetArray();

    for (const auto& pair : map)
    {
        const auto& preset_name = pair.first;
        const auto& preset = pair.second;

        rapidjson::Value v_preset;
        _json_write_preset_object(preset_name, preset, v_preset, doc);

        array.PushBack(v_preset, doc.GetAllocator());
    }

    value.AddMember(rapidjson::StringRef(name), array, doc.GetAllocator());
}

template<typename Preset>
ParsingErrorFlags _json_read_presets_map_member(
    const char* name,
    PresetMap<Preset>& map,
    const rapidjson::Value& value,
    bool disallow_empty_keys = true)
{
    if (!value.IsObject()) return userdata::ParsingError_InvalidData;

    auto it = value.FindMember(name);

    if (it == value.MemberEnd())
        return ParsingError_MissingData;
    else if (!it->value.IsArray())
        return ParsingError_InvalidData;

    auto& member = it->value;
    map.clear();

    std::string preset_name;
    Preset preset;

    int error = ParsingError_None;
    for (rapidjson::SizeType i = 0; i < member.Size(); ++i)
    {
        auto& v_preset = member[i];
        error &= _json_read_preset_object(preset_name, preset, v_preset);

        if (!(disallow_empty_keys && preset_name == ""))
        {
            map[preset_name] = preset;
        }
    }

    return error;
}

void _json_write_recent_elements_member(
    const char* name,
    const RecentElementSystem& system,
    rapidjson::Value& value,
    rapidjson::Document& doc)
{
    assert(value.IsObject());

    rapidjson::Value array;
    array.SetArray();

    // Most recent paths are written first
    for (const auto& element : system.get_elements())
    {
        rapidjson::Value v_element;
        v_element.SetString(element, doc.GetAllocator());

        array.PushBack(v_element, doc.GetAllocator());
    }

    value.AddMember(rapidjson::StringRef(name), array, doc.GetAllocator());
}

ParsingErrorFlags _json_read_recent_elements_member(
    const char* name,
    RecentElementSystem& system,
    const rapidjson::Value& value)
{
    auto it = value.FindMember(name);

    if (it == value.MemberEnd())
        return ParsingError_MissingData;
    else if (!it->value.IsArray())
        return ParsingError_InvalidData;

    auto& member = it->value;
    system.clear();

    // Most recent paths are written first : so to preserve the order we need to read in the
    // opposite direction
    for (rapidjson::SizeType i = member.Size() - 1; i < member.Size(); --i)
    {
        auto& v_element = member[i];
        if (!v_element.IsString()) return ParsingError_InvalidData;

        system.push_element(v_element.GetString());
    }

    return ParsingError_None;
}

} // namespace

namespace userdata
{
std::string Userdata::_to_json_string() const
{
    rapidjson::Document document;
    document.SetObject();

    _json_write_presets_map_member(
        "gpu_filter_presets", gpu_resource_filter_presets, document, document);
    _json_write_presets_map_member("blob_filter_presets", blob_filter_presets, document, document);
    _json_write_presets_map_member("layout_presets", window_layout_presets, document, document);
    _json_write_recent_elements_member(
        "recently_opened_paths", recently_opened_paths, document, document);
    _json_write_primitive_member("style", style_index, document, document);
    _json_write_string_member("layout", layout_name, document, document);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    return buffer.GetString();
}

ParsingErrorFlags Userdata::_load_from_json_string(const std::string& json)
{
    rapidjson::Document document;
    document.Parse(json.c_str());

    if (!document.IsObject()) return ParsingError_InvalidData;

    int error = ParsingError_None;
    error &=
        _json_read_presets_map_member("gpu_filter_presets", gpu_resource_filter_presets, document);
    error &= _json_read_presets_map_member("blob_filter_presets", blob_filter_presets, document);
    error &= _json_read_presets_map_member("layout_presets", window_layout_presets, document);
    error &=
        _json_read_recent_elements_member("recently_opened_paths", recently_opened_paths, document);
    error &= _json_read_primitive_member("style", style_index, document);
    error &= _json_read_string_member("layout", layout_name, document);

    return error;
}

bool Userdata::save_to_disk() const
{
    std::string path = _userdata_dir();

    bool ok = _create_dir_if_inexistent(path.c_str());
    if (!ok)
    {
        return false;
    }

    path = _userdata_file();
    std::ofstream file(path.c_str(), std::ios::out | std::ios::trunc);
    if (!file)
    {
        return false;
    }

    std::string file_contents = _to_json_string();
    file << file_contents;

    return !file.fail();
}

ParsingErrorFlags Userdata::load_from_disk()
{
    std::string path = _userdata_file();

    std::ifstream file(path);
    if (!file)
    {
        // If the file does not exist, this is not an error, it's just the first time that
        // the monitoring app is launched
        return true;
    }

    std::stringstream sstream;
    sstream << file.rdbuf();
    if (file.fail())
    {
        return false;
    }

    return _load_from_json_string(sstream.str());
}

RecentElementSystem::RecentElementSystem(size_t max_elements) : _max_elements(max_elements) {}

void RecentElementSystem::push_element(const char* element)
{
    // If the given element already is in the list, we want to move it to the top
    bool moved = false;
    for (auto it = _elements.begin(); it != _elements.end(); ++it)
    {
        if (*it == element)
        {
            _elements.splice(_elements.begin(), _elements, it);
            moved = true;
            break;
        }
    }

    if (!moved)
    {
        _elements.push_front(element);
        if (_elements.size() >= _max_elements) _elements.resize(_max_elements);
    }

    _update_view_vector();
}

void RecentElementSystem::remove_element(const char* element)
{
    _elements.remove(element);
    _update_view_vector();
}

void RecentElementSystem::_update_view_vector()
{
    _views.resize(_elements.size());

    size_t i = 0;
    for (const auto& element : _elements)
    {
        _views[i] = element.c_str();
        i++;
    }
}
} // namespace userdata
