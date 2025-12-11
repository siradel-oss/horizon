#include "hrz/monitoring/monitoring.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <optional>

namespace
{
static constexpr uint32_t MESSAGE_ENCODING_VERSION = 1;
}

namespace hrz_monitoring
{
struct MessageBuffer
{
    std::string string;

    mutable std::optional<google::protobuf::io::StringOutputStream> sos;
    mutable std::optional<google::protobuf::io::CodedOutputStream> cos;

    explicit MessageBuffer(size_t reserve)
    {
        string.reserve(reserve);
        string.clear();
    }

    void create_streams()
    {
        sos.emplace(&string);
        cos.emplace(&*sos);
    }

    void destroy_streams() const
    {
        cos.reset();
        sos.reset();
    }
};

MessageBuffer* create_buffer(size_t reserved_size)
{
    return new MessageBuffer(reserved_size);
}

void destroy_buffer(MessageBuffer* buffer)
{
    delete buffer;
}

void reset_buffer(MessageBuffer* buffer)
{
    buffer->destroy_streams();
    buffer->string.clear();
}

bool push_message(MessageBuffer* buffer, const hrz_monitoring_proto::MonitoringMessage& message)
{
    if (!buffer->cos) buffer->create_streams();

    buffer->cos->WriteLittleEndian32(MESSAGE_ENCODING_VERSION);
    if (buffer->cos->HadError())
    {
        return false;
    }

    buffer->cos->WriteLittleEndian32(message.ByteSizeLong());
    if (buffer->cos->HadError())
    {
        return false;
    }

    if (!message.SerializeToCodedStream(&*buffer->cos))
    {
        return false;
    }

    return true;
}

bool push_messages(MessageBuffer* buffer, const hrz_monitoring_proto::MonitoringMessages& messages)
{
    for (const auto& message : messages.messages())
    {
        if (!push_message(buffer, message))
        {
            return false;
        }
    }
    return true;
}

void append_messages(MessageBuffer* buffer, const MessageBuffer* appended)
{
    if (!buffer->cos) buffer->create_streams();

    const auto appended_span = hrz_monitoring::get_written_data(appended);
    buffer->cos->WriteRaw(appended_span.data(), appended_span.size());
}

void parse_messages(std::span<const std::byte> data, const MessageCallback& callback)
{
    google::protobuf::io::CodedInputStream coded_istream(
        (const unsigned char*)data.data(), data.size());

    uint32_t encoding_version;
    uint32_t incoming_size;
    hrz_monitoring_proto::MonitoringMessage message;

    while (coded_istream.ReadLittleEndian32(&encoding_version))
    {
        if (encoding_version != MESSAGE_ENCODING_VERSION)
        {
            break;
        }

        if (!coded_istream.ReadLittleEndian32(&incoming_size) || incoming_size == 0)
        {
            break;
        }

        // We prevent the input stream from reading more bytes than the amount we have written
        // in the buffer, otherwise it won't be able to parse the message
        auto limit = coded_istream.PushLimit(incoming_size);

        if (!message.ParseFromCodedStream(&coded_istream))
        {
            break;
        }

        coded_istream.PopLimit(limit);

        callback(&message);
    }
}

std::span<const std::byte> get_written_data(const MessageBuffer* buffer)
{
    // While a StringOutputStream exists, the data of its destination string is invalid and
    // arbitrary, which is why we must destroy a buffer's string before reading into it.
    buffer->destroy_streams();

    return std::span{(const std::byte*)buffer->string.data(), buffer->string.size()};
}

uint32_t get_message_encoding_version()
{
    return MESSAGE_ENCODING_VERSION;
}

} // namespace hrz_monitoring
