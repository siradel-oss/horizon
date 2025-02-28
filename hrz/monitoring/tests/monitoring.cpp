#include <hrz_monitoring.h>

#include <google/protobuf/arena.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <gtest/gtest.h>

#include <iostream>
#include <limits>
#include <vector>

#define BUFFER_SIZE 4096

void write_message(
    google::protobuf::io::CodedOutputStream* ostream,
    const hrz_monitoring_proto::MonitoringMessage* message)
{
    ostream->WriteLittleEndian32(hrz_monitoring::get_message_encoding_version());
    ostream->WriteLittleEndian32(message->ByteSizeLong());
    message->SerializeToCodedStream(ostream);
}

TEST(MessageBuffer, parsing)
{
    google::protobuf::Arena arena;

    unsigned char ground_truth_data[BUFFER_SIZE] = {0};
    google::protobuf::io::ArrayOutputStream array_ostream(ground_truth_data, BUFFER_SIZE);
    google::protobuf::io::CodedOutputStream coded_ostream(&array_ostream);

    auto sample = google::protobuf::Arena::CreateMessage<hrz_monitoring_proto::Sample>(&arena);
    sample->set_entry(20);
    sample->set_exit(60);

    auto* child1 = sample->add_children();
    child1->set_entry(20);
    child1->set_exit(40);

    auto* child2 = sample->add_children();
    child2->set_entry(40);
    child2->set_exit(60);

    auto message1 =
        google::protobuf::Arena::CreateMessage<hrz_monitoring_proto::MonitoringMessage>(&arena);
    message1->set_allocated_sample(sample);

    auto metric = google::protobuf::Arena::CreateMessage<hrz_monitoring_proto::Metric>(&arena);

    metric->set_name("TestValue");

    auto label = metric->add_labels();
    label->set_name("TestLabel");
    label->set_value("TestLabelValue");

    auto update = metric->add_updates();
    update->set_gauge(42.0);

    auto message2 =
        google::protobuf::Arena::CreateMessage<hrz_monitoring_proto::MonitoringMessage>(&arena);
    message2->set_allocated_metric(metric);

    auto histogram = google::protobuf::Arena::CreateMessage<hrz_monitoring_proto::Metric>(&arena);

    histogram->set_name("TestHistogram");

    auto histogram_update = histogram->add_updates();

    histogram_update->mutable_histogram()->add_bucket_counts(0);
    histogram_update->mutable_histogram()->add_bucket_counts(1);
    histogram_update->mutable_histogram()->add_bucket_counts(2);

    histogram_update->mutable_histogram()->add_bucket_values(0.0);
    histogram_update->mutable_histogram()->add_bucket_values(1.0);
    histogram_update->mutable_histogram()->add_bucket_values(
        std::numeric_limits<double>::infinity());

    auto message3 =
        google::protobuf::Arena::CreateMessage<hrz_monitoring_proto::MonitoringMessage>(&arena);
    message3->set_allocated_metric(histogram);

    write_message(&coded_ostream, message1);
    write_message(&coded_ostream, message2);
    write_message(&coded_ostream, message3);

    const size_t written_size = coded_ostream.ByteCount();

    std::vector<hrz_monitoring_proto::MonitoringMessage> parsed_messages;

    hrz_monitoring::MessageCallback callback =
        [&parsed_messages](const hrz_monitoring_proto::MonitoringMessage* message)
    { parsed_messages.push_back(*message); };

    hrz_monitoring::parse_messages(
        gsl::span<const std::byte>((const std::byte*)ground_truth_data, written_size), callback);

    EXPECT_EQ(parsed_messages.size(), 3);

    EXPECT_EQ(parsed_messages[0].kind_case(), hrz_monitoring_proto::MonitoringMessage::kSample);

    EXPECT_EQ(parsed_messages[0].sample().children_size(), 2);
    EXPECT_EQ(parsed_messages[0].sample().entry(), 20);
    EXPECT_EQ(parsed_messages[0].sample().exit(), 60);
    EXPECT_EQ(parsed_messages[0].sample().aggregation_count(), 0);
    EXPECT_EQ(parsed_messages[0].sample().recursion_max(), 0);

    EXPECT_EQ(parsed_messages[0].sample().children(0).entry(), 20);
    EXPECT_EQ(parsed_messages[0].sample().children(0).exit(), 40);
    EXPECT_EQ(parsed_messages[0].sample().children(1).entry(), 40);
    EXPECT_EQ(parsed_messages[0].sample().children(1).exit(), 60);

    EXPECT_EQ(parsed_messages[1].kind_case(), hrz_monitoring_proto::MonitoringMessage::kMetric);

    EXPECT_EQ(parsed_messages[1].metric().labels_size(), 1);
    EXPECT_STREQ(parsed_messages[1].metric().name().c_str(), "TestValue");

    EXPECT_EQ(parsed_messages[1].metric().updates_size(), 1);
    EXPECT_EQ(parsed_messages[1].metric().updates(0).has_gauge(), true);
    EXPECT_DOUBLE_EQ(parsed_messages[1].metric().updates(0).gauge(), 42.0);

    EXPECT_EQ(parsed_messages[2].kind_case(), hrz_monitoring_proto::MonitoringMessage::kMetric);

    EXPECT_EQ(parsed_messages[2].metric().labels_size(), 0);
    EXPECT_EQ(parsed_messages[2].metric().updates(0).has_histogram(), true);
    EXPECT_EQ(parsed_messages[2].metric().updates(0).histogram().bucket_counts_size(), 3);
    EXPECT_EQ(parsed_messages[2].metric().updates(0).histogram().bucket_values_size(), 3);

    EXPECT_STREQ(parsed_messages[2].metric().name().c_str(), "TestHistogram");
    EXPECT_EQ(parsed_messages[2].metric().updates(0).histogram().total_count(), 0.0);

    EXPECT_EQ(parsed_messages[2].metric().updates(0).histogram().bucket_counts(0), 0);
    EXPECT_EQ(parsed_messages[2].metric().updates(0).histogram().bucket_counts(1), 1);
    EXPECT_EQ(parsed_messages[2].metric().updates(0).histogram().bucket_counts(2), 2);

    EXPECT_EQ(parsed_messages[2].metric().updates(0).histogram().bucket_values(0), 0.0);
    EXPECT_EQ(parsed_messages[2].metric().updates(0).histogram().bucket_values(1), 1.0);
    EXPECT_EQ(
        parsed_messages[2].metric().updates(0).histogram().bucket_values(2),
        std::numeric_limits<double>::infinity());
}

TEST(MessageBuffer, serializing)
{
    google::protobuf::Arena arena;

    unsigned char ground_truth_data[BUFFER_SIZE] = {0};
    google::protobuf::io::ArrayOutputStream array_ostream(ground_truth_data, BUFFER_SIZE);
    google::protobuf::io::CodedOutputStream coded_ostream(&array_ostream);

    auto messages =
        google::protobuf::Arena::CreateMessage<hrz_monitoring_proto::MonitoringMessages>(&arena);

    auto sample = google::protobuf::Arena::CreateMessage<hrz_monitoring_proto::Sample>(&arena);
    sample->set_entry(20);
    sample->set_exit(60);

    auto* child1 = sample->add_children();
    child1->set_entry(20);
    child1->set_exit(40);

    auto* child2 = sample->add_children();
    child2->set_entry(40);
    child2->set_exit(60);

    auto message1 = messages->add_messages();
    message1->set_allocated_sample(sample);

    auto metric = google::protobuf::Arena::CreateMessage<hrz_monitoring_proto::Metric>(&arena);
    ;
    metric->set_name("TestValue");
    metric->add_updates()->set_gauge(42.0);

    auto message2 = messages->add_messages();
    message2->set_allocated_metric(metric);

    write_message(&coded_ostream, message1);
    write_message(&coded_ostream, message2);

    const size_t written_size = coded_ostream.ByteCount();

    hrz_monitoring::MessageBuffer* buffer = hrz_monitoring::create_buffer();
    EXPECT_EQ(hrz_monitoring::get_written_data(buffer).size_bytes(), 0);

    hrz_monitoring::push_messages(buffer, *messages);
    EXPECT_EQ(hrz_monitoring::get_written_data(buffer).size_bytes(), written_size);

    auto buffer_data = hrz_monitoring::get_written_data(buffer);
    ASSERT_EQ(buffer_data.size(), written_size);
    ASSERT_TRUE(memcmp(buffer_data.data(), ground_truth_data, written_size) == 0);
}

TEST(MessageBuffer, reset)
{
    hrz_monitoring::MessageBuffer* buffer = hrz_monitoring::create_buffer();

    size_t first_size = 0;

    {
        hrz_monitoring_proto::MonitoringMessages messages;

        auto* message = messages.add_messages();
        auto* sample = message->mutable_sample();
        sample->set_entry(20);
        sample->set_exit(60);

        message = messages.add_messages();
        sample = message->mutable_sample();
        sample->set_entry(80);
        sample->set_exit(90);

        hrz_monitoring::push_messages(buffer, messages);
        first_size = hrz_monitoring::get_written_data(buffer).size();
    }

    hrz_monitoring::reset_buffer(buffer);
    EXPECT_EQ(hrz_monitoring::get_written_data(buffer).size(), 0);

    {
        hrz_monitoring_proto::MonitoringMessages messages;

        auto* message = messages.add_messages();
        auto* sample = message->mutable_sample();
        sample->set_entry(20);
        sample->set_exit(60);

        hrz_monitoring::push_messages(buffer, messages);
    }

    EXPECT_LT(hrz_monitoring::get_written_data(buffer).size(), first_size);
}

TEST(MessageBuffer, append)
{
    hrz_monitoring::MessageBuffer* buffer1 = hrz_monitoring::create_buffer();
    hrz_monitoring::MessageBuffer* buffer2 = hrz_monitoring::create_buffer();

    {
        hrz_monitoring_proto::MonitoringMessages messages;

        auto* message = messages.add_messages();
        auto* sample = message->mutable_sample();
        sample->set_entry(10);
        sample->set_exit(11);

        hrz_monitoring::push_messages(buffer1, messages);
    }

    {
        hrz_monitoring_proto::MonitoringMessages messages;

        auto* message = messages.add_messages();
        auto* sample = message->mutable_sample();
        sample->set_entry(12);
        sample->set_exit(13);

        hrz_monitoring::push_messages(buffer2, messages);
    }

    size_t size1 = hrz_monitoring::get_written_data(buffer1).size();
    size_t size2 = hrz_monitoring::get_written_data(buffer2).size();

    hrz_monitoring::append_messages(buffer1, buffer2);

    ASSERT_EQ(hrz_monitoring::get_written_data(buffer1).size(), size1 + size2);

    size_t parsed = 0;
    hrz_monitoring::parse_messages(
        hrz_monitoring::get_written_data(buffer1),
        [&](const hrz_monitoring_proto::MonitoringMessage* msg)
        {
            if (parsed == 0)
            {
                EXPECT_EQ(msg->sample().entry(), 10);
                EXPECT_EQ(msg->sample().exit(), 11);
            }
            else if (parsed == 1)
            {
                EXPECT_EQ(msg->sample().entry(), 12);
                EXPECT_EQ(msg->sample().exit(), 13);
            }
            parsed++;
        });

    ASSERT_EQ(parsed, 2);
}
