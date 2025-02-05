#pragma once

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/descriptor_database.h>
#include <google/protobuf/dynamic_message.h>

namespace hrz::migration
{
struct MessageFactory
{
    google::protobuf::FileDescriptorSet descriptor_set;
    google::protobuf::SimpleDescriptorDatabase database;
    google::protobuf::DescriptorPool pool;
    google::protobuf::DynamicMessageFactory factory;

    MessageFactory(const google::protobuf::FileDescriptorSet& descriptor_set_) :
        descriptor_set(descriptor_set_), database(), pool(&database), factory(&pool)
    {
        for (const auto& file : descriptor_set.file())
        {
            database.Add(file);
        }
    }
};

#define LIST_WRAPPER_TYPES                    \
    WRAPPER_TYPE(string, std::string, String) \
    WRAPPER_TYPE(uint32, uint32_t, UInt32)    \
    WRAPPER_TYPE(uint64, uint64_t, UInt64)    \
    WRAPPER_TYPE(fixed64, uint64_t, UInt64)   \
    WRAPPER_TYPE(int32, int32_t, Int32)       \
    WRAPPER_TYPE(int64, int64_t, Int64)       \
    WRAPPER_TYPE(sfixed64, int64_t, Int64)    \
    WRAPPER_TYPE(float, float, Float)         \
    WRAPPER_TYPE(double, double, Double)      \
    WRAPPER_TYPE(bool, bool, Bool)

struct DynamicMessage
{
    MessageFactory* factory;
    google::protobuf::Message* msg;

    DynamicMessage(MessageFactory* factory_, google::protobuf::Message* msg_) :
        factory(factory_), msg(msg_)
    {
    }

    const google::protobuf::FieldDescriptor* _get_field_descriptor(const char* field_name) const
    {
        const auto* descriptor = msg->GetDescriptor()->FindFieldByName(field_name);
        assert(descriptor);
        return descriptor;
    }

    const google::protobuf::OneofDescriptor* _get_oneof_descriptor(const char* field_name) const
    {
        const auto* descriptor = msg->GetDescriptor()->FindOneofByName(field_name);
        assert(descriptor);
        return descriptor;
    }

    const google::protobuf::EnumValueDescriptor* _get_enum_value_descriptor(
        const char* field_name,
        const char* enum_value) const
    {
        auto* field_desc = _get_field_descriptor(field_name);
        assert(field_desc->type() == google::protobuf::FieldDescriptor::TYPE_ENUM);
        return field_desc->enum_type()->FindValueByName(enum_value);
    }

    std::string get_type_name() const { return msg->GetTypeName(); }

    DynamicMessage create_message(const char* full_type_name)
    {
        const auto* desc = factory->pool.FindMessageTypeByName(full_type_name);
        assert(desc);

        return DynamicMessage(factory, factory->factory.GetPrototype(desc)->New());
    }

    bool has_field(const char* field_name) const
    {
        return msg->GetReflection()->HasField(*msg, _get_field_descriptor(field_name));
    }

    bool has_oneof(const char* oneof_name) const
    {
        return msg->GetReflection()->HasOneof(*msg, _get_oneof_descriptor(oneof_name));
    }

    int field_size(const char* field_name) const
    {
        return msg->GetReflection()->FieldSize(*msg, _get_field_descriptor(field_name));
    }

    void clear_field(const char* field_name)
    {
        msg->GetReflection()->ClearField(msg, _get_field_descriptor(field_name));
    }

    void clear_oneof(const char* oneof_name)
    {
        msg->GetReflection()->ClearOneof(msg, _get_oneof_descriptor(oneof_name));
    }

    const std::string& get_oneof_field_name(const char* oneof_name) const
    {
        return msg->GetReflection()
            ->GetOneofFieldDescriptor(*msg, _get_oneof_descriptor(oneof_name))
            ->name();
    }

    void remove_repeated(const char* field_name, int index)
    {
        auto* refl = msg->GetReflection();
        const auto* field_desc = _get_field_descriptor(field_name);
        int size = refl->FieldSize(*msg, field_desc);

        // Shift the element to remove to the end
        for (int i = index; i < size - 1; ++i)
        {
            refl->SwapElements(msg, field_desc, i, i + 1);
        }

        refl->RemoveLast(msg, field_desc);
    }

#define WRAPPER_TYPE(NAME, CPP, PB)                                                    \
    CPP get_##NAME(const char* field_name) const                                       \
    {                                                                                  \
        return msg->GetReflection()->Get##PB(*msg, _get_field_descriptor(field_name)); \
    }
    LIST_WRAPPER_TYPES
#undef WRAPPER_TYPE

    const std::string& get_enum(const char* field_name) const
    {
        const auto* enum_desc =
            msg->GetReflection()->GetEnum(*msg, _get_field_descriptor(field_name));
        assert(enum_desc);
        return enum_desc->name();
    }

    DynamicMessage get_message(const char* field_name) const
    {
        auto* submsg = msg->GetReflection()->MutableMessage(
            msg, _get_field_descriptor(field_name), &factory->factory);
        return DynamicMessage(factory, submsg);
    }

#define WRAPPER_TYPE(NAME, CPP, PB)                                  \
    CPP get_repeated_##NAME(const char* field_name, int index) const \
    {                                                                \
        return msg->GetReflection()->GetRepeated##PB(                \
            *msg, _get_field_descriptor(field_name), index);         \
    }
    LIST_WRAPPER_TYPES
#undef WRAPPER_TYPE

    const std::string& get_repeated_enum(const char* field_name, int index) const
    {
        const auto* enum_desc =
            msg->GetReflection()->GetRepeatedEnum(*msg, _get_field_descriptor(field_name), index);
        assert(enum_desc);
        return enum_desc->name();
    }

    DynamicMessage get_repeated_message(const char* field_name, int index) const
    {
        auto* submsg = msg->GetReflection()->MutableRepeatedMessage(
            msg, _get_field_descriptor(field_name), index);
        return DynamicMessage(factory, submsg);
    }

#define WRAPPER_TYPE(NAME, CPP, PB)                                                   \
    void set_##NAME(const char* field_name, CPP value)                                \
    {                                                                                 \
        msg->GetReflection()->Set##PB(msg, _get_field_descriptor(field_name), value); \
    }
    LIST_WRAPPER_TYPES
#undef WRAPPER_TYPE

    void set_enum(const char* field_name, const char* value)
    {
        msg->GetReflection()->SetEnum(
            msg, _get_field_descriptor(field_name), _get_enum_value_descriptor(field_name, value));
    }

    void set_enum(const char* file_name, const std::string& value)
    {
        set_enum(file_name, value.c_str());
    }

    void set_message(const char* field_name, const DynamicMessage& value)
    {
        msg->GetReflection()
            ->MutableMessage(msg, _get_field_descriptor(field_name), &factory->factory)
            ->MergeFrom(*value.msg);
    }

#define WRAPPER_TYPE(NAME, CPP, PB)                                        \
    void set_repeated_##NAME(const char* field_name, int index, CPP value) \
    {                                                                      \
        msg->GetReflection()->SetRepeated##PB(                             \
            msg, _get_field_descriptor(field_name), index, value);         \
    }
    LIST_WRAPPER_TYPES
#undef WRAPPER_TYPE

    void set_repeated_enum(const char* field_name, int index, const char* value)
    {
        msg->GetReflection()->SetRepeatedEnum(
            msg, _get_field_descriptor(field_name), index,
            _get_enum_value_descriptor(field_name, value));
    }

    void set_repeated_message(const char* field_name, int index, const DynamicMessage& value)
    {
        msg->GetReflection()
            ->MutableRepeatedMessage(msg, _get_field_descriptor(field_name), index)
            ->MergeFrom(*value.msg);
    }

#define WRAPPER_TYPE(NAME, CPP, PB)                                                   \
    void add_##NAME(const char* field_name, CPP value)                                \
    {                                                                                 \
        msg->GetReflection()->Add##PB(msg, _get_field_descriptor(field_name), value); \
    }
    LIST_WRAPPER_TYPES
#undef WRAPPER_TYPE

    void add_repeated_enum(const char* field_name, const char* value)
    {
        msg->GetReflection()->AddEnum(
            msg, _get_field_descriptor(field_name), _get_enum_value_descriptor(field_name, value));
    }

    void add_repeated_message(const char* field_name, const DynamicMessage& value)
    {
        msg->GetReflection()
            ->AddMessage(msg, _get_field_descriptor(field_name), &factory->factory)
            ->MergeFrom(*value.msg);
    }

    // Deep copy a message field by field.
    // Can be used to copy a message from a source message to a destination message
    // when the protocol has changed and the message is not at the same location.
    void copy_message(const char* field_name, const DynamicMessage& value)
    {
        msg->GetReflection()
            ->MutableMessage(msg, _get_field_descriptor(field_name), &factory->factory)
            ->MergeFromString(value.msg->SerializeAsString());
    }
};

} // namespace hrz::migration
