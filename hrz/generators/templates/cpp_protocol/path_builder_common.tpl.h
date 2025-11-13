#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <concepts>

#include "hrz/protocol/scene_model.pb.h"
#include "hrz/protocol/wrappers.pb.h"

namespace hrz_proto
{

template<std::movable T, typename PathBuilder>
class SceneModelPathBuilder
{
protected:
    T                   _accessor;
    hrz_proto::Path     _path;

public:
    SceneModelPathBuilder(T&& accessor, hrz_proto::Path&& path) :
        _accessor{std::move(accessor)},
        _path{std::move(path)}
    {}

    template<typename Arg>
    explicit SceneModelPathBuilder(Arg&& accessor)
        requires std::constructible_from<T, Arg&&>
        : _accessor{std::move(accessor)}
    {}

    const hrz_proto::Path& get_path() const
    {
        return _path;
    }

    PathBuilder clone() const
        requires std::copyable<T>
    {
        return PathBuilder{T{_accessor}, hrz_proto::Path{_path}};
    }
};

template<std::movable T, typename WrapperType, typename PrimitiveType>
class PrimitivePathBuilder : public SceneModelPathBuilder<T, PrimitivePathBuilder<T, WrapperType, PrimitiveType>>
{
    using Base = SceneModelPathBuilder<T, PrimitivePathBuilder<T, WrapperType, PrimitiveType>>;

public:
    PrimitivePathBuilder(T&& accessor, hrz_proto::Path&& path) :
        Base{std::move(accessor), std::move(path)}
    {}

    void set(const PrimitiveType& value) &&
    {
        WrapperType wrapper;
        wrapper.set_value(value);
        Base::_accessor.set_raw(std::move(Base::_path), wrapper.SerializeAsString());
    }

    PrimitiveType get() &&
    {
        auto raw = Base::_accessor.get_raw(std::move(Base::_path));
        WrapperType wrapper;
        wrapper.ParseFromString(raw);
        return wrapper.value();
    }
};

template<std::movable T, typename E>
class EnumPathBuilder : public SceneModelPathBuilder<T, EnumPathBuilder<T, E>>
{
    using Base = SceneModelPathBuilder<T, EnumPathBuilder<T, E>>;

public:
    EnumPathBuilder(T&& accessor, hrz_proto::Path&& path) :
        Base{std::move(accessor), std::move(path)}
    {}

    void set(E value) &&
    {
        ::hrz_proto::UInt32Value wrapper;
        wrapper.set_value((uint32_t)value);
        Base::_accessor.set_raw(std::move(Base::_path), wrapper.SerializeAsString());
    }

    E get() &&
    {
        auto raw = Base::_accessor.get_raw(std::move(Base::_path));
        ::hrz_proto::UInt32Value wrapper;
        wrapper.ParseFromString(raw);
        return (E)wrapper.value();
    }
};

// Here we forward-declare path builders for recursive types.
{% for type in to_forward_declare %}
template<std::movable T> class {{ type|to_short_type_name }}PathBuilder;
{% endfor %}

}

