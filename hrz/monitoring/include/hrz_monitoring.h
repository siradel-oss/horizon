#pragma once

#include "hrz_monitoring_protocol.h"

#include <gsl/gsl-lite.hpp>

#include <functional>

namespace hrz_monitoring
{
using namespace hrz_monitoring_proto;

using MessageCallback = std::function<void(const hrz_monitoring_proto::MonitoringMessage*)>;

struct MessageBuffer;

MessageBuffer* create_buffer(size_t reserved_size = 0);
void destroy_buffer(MessageBuffer* buffer);

void reset_buffer(MessageBuffer* buffer);

bool push_message(MessageBuffer* buffer, const hrz_monitoring_proto::MonitoringMessage& message);
bool push_messages(MessageBuffer* buffer, const hrz_monitoring_proto::MonitoringMessages& messages);

void append_messages(MessageBuffer* buffer, const MessageBuffer* appended);

void parse_messages(gsl::span<const std::byte> data, const MessageCallback& callback);

gsl::span<const std::byte> get_written_data(const MessageBuffer* buffer);

uint32_t get_message_encoding_version();

} // namespace hrz_monitoring
