#pragma once

#include "hrz/protocol/scene_model.pb.h"
#include "hrz/protocol/services.h"

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////


namespace hrz_api
{

class Api;
class Backend;

{% for s in protocol.services %}
/* {{ s.documentation }} */
class {{ s.full_name|last(".") }}
{
public:
    {{ s.full_name|last(".") }}(Backend* backend) :
        _backend(backend)
    {}

    {% for m in s.methods %}
    /* {{ m.documentation|indent(4) }} */
    void {{ m.name|snake_case }}(
        const ::{{ m.input|rejoin(".", "::") }}& input,
        ::{{ m.output|rejoin(".", "::") }}& output);

    {% endfor %}

private:
    Backend* _backend;
};

{% endfor %}

class Backend
{
public:
    virtual ~Backend() = default;

    virtual std::vector<uint8_t> rpc(
        uint32_t service,
        uint32_t method,
        const void* data,
        size_t size) = 0;

    void rpc_msg(
        uint32_t service,
        uint32_t method,
        const google::protobuf::MessageLite& input,
        google::protobuf::MessageLite& output);
};

class Api
{
public:
    static std::shared_ptr<Api> create(std::shared_ptr<Backend> backend)
    {
        return std::shared_ptr<Api>(new Api(backend));
    }

private:
    Api(std::shared_ptr<Backend> backend) :
        _backend(backend)
        {% for s in protocol.services %}
        , {{ s.full_name|last(".")|snake_case }}(backend.get())
        {% endfor %}
    {
    }

    std::shared_ptr<Backend> _backend;

public:
    {% for s in protocol.services %}
    {{ s.full_name|last(".") }} {{ s.full_name|last(".")|snake_case }};
    {% endfor %}
};

/**
 * To use with hrz_proto path builders.
 * This calls the APIs using paths built by path builders.
 */
class SceneModelAccessor
{
public:
    SceneModelAccessor() = default;

    SceneModelAccessor(std::shared_ptr<Api> api) :
        _api(api)
    {}

    SceneModelAccessor(const SceneModelAccessor&) = default;

    template<typename T>
    void set(hrz_proto::Path path, const T& payload)
    {
        hrz_proto::SceneModelSet msg_in;
        *msg_in.mutable_path() = std::move(path);
        msg_in.set_payload(std::move(payload.SerializeAsString()));

        hrz_proto::Void v;

        _api->scene_model_service.set(msg_in, v);
    }

    void set_raw(hrz_proto::Path path, std::string data)
    {
        hrz_proto::SceneModelSet msg_in;
        *msg_in.mutable_path() = std::move(path);
        msg_in.set_payload(std::move(data));

        hrz_proto::Void v;

        _api->scene_model_service.set(msg_in, v);
    }

    template<typename T>
    T get(const hrz_proto::Path& path)
    {
        hrz_proto::BytesValue response;
        _api->scene_model_service.get(path, response);

        T value;
        value.ParseFromString(response.value());

        return value;
    }

    std::string get_raw(const hrz_proto::Path& path)
    {
        hrz_proto::BytesValue response;
        _api->scene_model_service.get(path, response);

        std::unique_ptr<std::string> str_ptr(response.release_value());
        return std::move(*str_ptr);
    }

    template<typename T>
    uint32_t add(hrz_proto::Path path, const T& payload)
    {
        hrz_proto::SceneModelSet msg_in;
        *msg_in.mutable_path() = std::move(path);
        msg_in.set_payload(std::move(payload.SerializeAsString()));

        hrz_proto::UInt32Value count;
        _api->scene_model_service.add(msg_in, count);
        return count.value();
    }

    uint32_t add_raw(hrz_proto::Path path, std::string data)
    {
        hrz_proto::SceneModelSet msg_in;
        *msg_in.mutable_path() = std::move(path);
        msg_in.set_payload(std::move(data));

        hrz_proto::UInt32Value count;
        _api->scene_model_service.add(msg_in, count);
        return count.value();
    }

    uint32_t remove(const hrz_proto::Path& path)
    {
        hrz_proto::UInt32Value count;
        _api->scene_model_service.remove(path, count);
        return count.value();
    }

    uint32_t count(const hrz_proto::Path& path)
    {
        hrz_proto::UInt32Value count;
        _api->scene_model_service.count(path, count);
        return count.value();
    }

private:
    std::shared_ptr<Api> _api;
};

}
