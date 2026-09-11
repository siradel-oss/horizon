// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <float.h>

#include <functional>
#include <stdint.h>
#include <string>
#include <utility>

namespace hrz_monitoring
{

struct MessageBuffer;

}

namespace hrz
{

struct ThreadMetricsRegistry;

namespace metrics
{

ThreadMetricsRegistry* create_thread_registry(bool enabled);
void destroy_thread_registry(ThreadMetricsRegistry*);

// Resets the values of "per-frame" metrics and merge consecutive operations (since the last call
// to this function) into one operation.
void finish_thread_registry_frame();

// Moves the thread registry's data to the main thread.
// It should be called regularly, otherwise that data will be discarded when it becomes too large.
void synchronize_thread_registry();

// The main thread has exclusive access to some of the following functions.
void init_main_thread();
void cleanup_main_thread();

// Can only be called by the main thread.
void set_metrics_registries_enabled(bool enabled);
// Can only be called by the main thread.
bool are_metrics_registries_enabled();
// Can only be called by the main thread.
void flush_messages(const std::function<void(const hrz_monitoring::MessageBuffer*)>& callback);

struct MetricDesc
{
    static constexpr size_t MAX_LABELS = 4;

    // Inner ID of the global registry. Used internally to identify a metric
    // across all registries. It will be filled by the registry.
    // Leave the field empty.
    uint64_t inner_id = 0;

    // Used to associate thread local metrics.
    // Leave the field empty.
    uint64_t hash = 0;

    const char* name = nullptr;

    // Labels can be used to specify additional metadata about a metric. For
    // instance it can specify the URL of an HTTP request.
    size_t label_count = 0;
    const char* label_names[MAX_LABELS];
    std::string label_values[MAX_LABELS];

    bool reset_on_frame = false;

    MetricDesc() = default;

    MetricDesc(
        const char* name_,
        bool reset_on_frame_,
        std::initializer_list<std::pair<const char*, std::string_view>> labels_);

    void push_label(const char* name, std::string_view value);
};

struct HistogramDesc : public MetricDesc
{
    double min_value = -DBL_MAX;
    double max_value = DBL_MAX;
    size_t bucket_count = 0;

    HistogramDesc(
        const char* name_,
        bool reset_on_frame_,
        double min_value_,
        double max_value_,
        size_t bucket_count_,
        std::initializer_list<std::pair<const char*, std::string_view>> labels_) :
        MetricDesc(name_, reset_on_frame_, labels_),
        min_value(min_value_),
        max_value(max_value_),
        bucket_count(bucket_count_)
    {
    }
};

// The following methods use a metric description as their input. This value
// should be reused between calls because the metrics system caches the id of
// the metric in the description structures. This avoids having to recompute
// the identity of the metric every time. When possible, this can be used in a
// static variable.

// Counters can only be incremented. When a frame is finished, their timestamp
// is the last time they were incremented. If you need to know the time of each
// trigger, use events instead.
void increment_counter(MetricDesc*);

// Gauges can be set, or a value can be added or subtracted. When a frame is
// finished, their timestamp is the last time the value was modified.
void set_gauge(MetricDesc*, double value);
void add_gauge(MetricDesc*, double value_to_add);
void subtract_gauge(MetricDesc*, double value_to_subtract);

// Histograms represent a linear distribution of values between their min and
// max value, in a specified number of buckets. There are special buckets for
// values below and above this range. When a frame is finished, their timestamp
// is the last time a value was added to the histogram.
void observe_histogram(HistogramDesc*, double value);

} // namespace metrics
} // namespace hrz

#define HRZ_DEFINE_STATIC_METRIC(name, reset_on_frame, ...)                                \
    static thread_local hrz::metrics::MetricDesc HRZ_CONCAT(hrz_static_metric_, __LINE__)( \
        (name), (reset_on_frame), __VA_ARGS__);

#define HRZ_INCREMENT_COUNTER_EX(name, reset_on_frame, ...)                           \
    do                                                                                \
    {                                                                                 \
        HRZ_DEFINE_STATIC_METRIC((name), (reset_on_frame), __VA_ARGS__);              \
        ::hrz::metrics::increment_counter(&HRZ_CONCAT(hrz_static_metric_, __LINE__)); \
    } while (0)

#define HRZ_GAUGE_EX(name, reset_on_frame, fn, value, ...)                      \
    do                                                                          \
    {                                                                           \
        HRZ_DEFINE_STATIC_METRIC((name), (reset_on_frame), __VA_ARGS__);        \
        ::hrz::metrics::fn(&HRZ_CONCAT(hrz_static_metric_, __LINE__), (value)); \
    } while (0)

#define HRZ_OBSERVE_HISTOGRAM_EX(                                                              \
    name, reset_on_frame, min_value, max_value, bucket_count, value, ...)                      \
    do                                                                                         \
    {                                                                                          \
        static hrz::metrics::HistogramDesc HRZ_CONCAT(hrz_static_metric_, __LINE__)(           \
            (name), (reset_on_frame), (min_value), (max_value), (bucket_count), __VA_ARGS__);  \
        ::hrz::metrics::observe_histogram(&HRZ_CONCAT(hrz_static_metric_, __LINE__), (value)); \
    } while (0)

#define HRZ_INCREMENT_COUNTER(name, ...) HRZ_INCREMENT_COUNTER_EX((name), false, __VA_ARGS__)

#define HRZ_INCREMENT_COUNTER_PER_FRAME(name, ...) \
    HRZ_INCREMENT_COUNTER_EX((name), true, __VA_ARGS__)

#define HRZ_SET_GAUGE(name, value, ...) HRZ_GAUGE_EX((name), false, set_gauge, (value), __VA_ARGS__)

#define HRZ_SET_GAUGE_PER_FRAME(name, value, ...) \
    HRZ_GAUGE_EX((name), true, set_gauge, (value), __VA_ARGS__)

#define HRZ_ADD_TO_GAUGE(name, value, ...) \
    HRZ_GAUGE_EX((name), false, add_gauge, (value), __VA_ARGS__)

#define HRZ_ADD_TO_GAUGE_PER_FRAME(name, value, ...) \
    HRZ_GAUGE_EX((name), true, add_gauge, (value), __VA_ARGS__)

#define HRZ_SUBTRACT_FROM_GAUGE(name, value, ...) \
    HRZ_GAUGE_EX((name), false, subtract_gauge, (value), __VA_ARGS__)

#define HRZ_SUBTRACT_FROM_GAUGE_PER_FRAME(name, value, ...) \
    HRZ_GAUGE_EX((name), true, subtract_gauge, (value), __VA_ARGS__)

#define HRZ_OBSERVE_HISTOGRAM(name, min_value, max_value, bucket_count, value, ...) \
    HRZ_OBSERVE_HISTOGRAM_EX(                                                       \
        (name), false, (min_value), (max_value), (bucket_count), (value), __VA_ARGS__)

#define HRZ_OBSERVE_HISTOGRAM_PER_FRAME(name, min_value, max_value, bucket_count, value, ...) \
    HRZ_OBSERVE_HISTOGRAM_EX(                                                                 \
        (name), true, (min_value), (max_value), (bucket_count), (value), __VA_ARGS__)
