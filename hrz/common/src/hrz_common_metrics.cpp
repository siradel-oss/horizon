#include "hrz_common_metrics.h"

#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_meta.h>
#include <hrz_fnd_time.h>
#include <hrz_fnd_variant.h>
#include <hrz_monitoring.h>

#include <google/protobuf/arena.h>

#include <algorithm>
#include <mutex>
#include <optional>
#include <variant>

using PbArena = google::protobuf::Arena;
static constexpr size_t MaxBufferedDataSize = 4 * 1024 * 1024;

struct HistogramValue
{
    double min_value;
    double max_value;
    size_t bucket_count;

    // First bucket is < min_value, last bucket is >= max_value.
    // So there are 2 extra buckets.
    std::unique_ptr<size_t[]> buckets;

    size_t total;

    HistogramValue(double min_value_, double max_value_, size_t bucket_count_) :
        min_value(min_value_),
        max_value(max_value_),
        bucket_count(bucket_count_),
        buckets(new size_t[bucket_count + 2])
    {
        assert(bucket_count > 0);
        assert(min_value < max_value);
        reset();
    }

    void reset()
    {
        std::fill(buckets.get(), buckets.get() + bucket_count + 2, 0);
        total = 0;
    }

    void observe(double value)
    {
        size_t index = 0;
        if (value < min_value)
        {
            index = 0;
        }
        else if (value >= max_value)
        {
            index = bucket_count + 1;
        }
        else
        {
            index = ((value - min_value) / (max_value - min_value)) * bucket_count + 1;
            assert(index >= 1 && index <= bucket_count);
        }

        buckets[index] += 1;
        total += 1;
    }
};

using MetricValue = std::variant<double, HistogramValue>;

struct Metric
{
    enum Type
    {
        Counter,
        Gauge,
        Histogram
    };

    Metric(Type type_, const hrz::metrics::MetricDesc* desc) :
        type(type_), name(desc->name), label_count(desc->label_count), value(0.0)
    {
        std::copy_n(desc->label_names, label_count, label_names);
        std::copy_n(desc->label_values, label_count, label_values);
    }

    Metric(Type type_, const hrz::metrics::HistogramDesc* desc) :
        type(type_),
        name(desc->name),
        label_count(desc->label_count),
        value(HistogramValue(desc->min_value, desc->max_value, desc->bucket_count))
    {
        std::copy_n(desc->label_names, label_count, label_names);
        std::copy_n(desc->label_values, label_count, label_values);
    }

    Type type;
    const char* name;

    size_t label_count;
    const char* label_names[hrz::metrics::MetricDesc::MAX_LABELS];
    std::string label_values[hrz::metrics::MetricDesc::MAX_LABELS];

    MetricValue value;

    void reset_value()
    {
        std::visit(
            [](auto& val)
            {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, double>)
                {
                    val = 0.0;
                }
                else if constexpr (std::is_same_v<T, HistogramValue>)
                {
                    val.reset();
                }
                else
                {
                    static_assert(hrz::always_false<T>, "Unhandled type in reset_value");
                }
            },
            value);
    }
};

using MetricIndexPool = hrz::GenIndexPool<uint64_t, 32, 32>;
using MetricPool = hrz::GenObjectPool<Metric, MetricIndexPool>;

using MetricId = uint64_t;
using MetricHash = uint64_t;

struct MetricOperation
{
    enum class Type
    {
        Set,
        Add,
        ObserveHistogram,
        Reset,
    };

    Type type;
    double data;
    int64_t timestamp_us;

    void apply(Metric* metric) const
    {
        switch (type)
        {
            case Type::Set:
                assert(std::holds_alternative<double>(metric->value));
                std::get<double>(metric->value) = data;
                break;

            case Type::Add:
                assert(std::holds_alternative<double>(metric->value));
                std::get<double>(metric->value) += data;
                break;

            case Type::ObserveHistogram:
                assert(std::holds_alternative<HistogramValue>(metric->value));
                std::get<HistogramValue>(metric->value).observe(data);
                break;

            case Type::Reset: metric->reset_value(); break;

            default: assert(false && "Unhandled");
        }
    }

    // Returns true if the `other` operation can be squashed with this one.
    // In that case, the `other` operation should be discarded as its effects are now included
    // in this one.
    bool combine_with(const MetricOperation* other)
    {
        // Reset operations should not replace other operations, but they can be replaced by others.
        if (other->type == Type::Reset) return false;

        // Histogram updates should be squashable, but it would require adding more data to the
        // update (the amount of times the same value was consecutively observed).
        // But histograms are barely used right now, so it hasn't been implemented.
        if (type == Type::ObserveHistogram) return false;

        // Set operations can replace any other operation on double values.
        if (other->type == Type::Set)
        {
            *this = *other;
            return true;
        }

        // Add operations can change the value of other operations on double values.
        // Reset operations carry no value, so they can't be replaced by Add.
        if (other->type == Type::Add && (type == Type::Set || type == Type::Add))
        {
            data += other->data;
            timestamp_us = other->timestamp_us;
            return true;
        }

        return false;
    }
};

struct SharedData
{
    std::mutex _mutex;

    PbArena _pb_arena;
    hrz_monitoring::MessageBuffer* _buffer;

    bool _registries_enabled = false;

    bool _main_thread_set = false;

    MetricPool _metric_pool;
    hrz::flat_hash_map<MetricHash, MetricId> _hash_to_id;
    hrz::flat_hash_map<MetricId, std::vector<MetricOperation>> _id_to_operations;

    SharedData() : _buffer(hrz_monitoring::create_buffer()) {}

    ~SharedData()
    {
        hrz_monitoring::destroy_buffer(_buffer);
        for (auto pair : _hash_to_id)
        {
            _metric_pool.release(pair.second);
        }
    }

    static MetricHash hash_metric_desc(const hrz::metrics::MetricDesc* desc)
    {
        // We sort the label names so that metrics with the same labels in
        // different orders are the same.

        size_t order[hrz::metrics::MetricDesc::MAX_LABELS];
        for (size_t i = 0; i < desc->label_count; ++i)
        {
            order[i] = i;
        }

        std::sort(
            order, order + desc->label_count,
            [&](size_t a, size_t b) -> bool
            { return strcmp(desc->label_names[a], desc->label_names[b]); });

        uint64_t hash = hrz::murmur3_x64_64(desc->name);
        for (size_t i = 0; i < desc->label_count; ++i)
        {
            size_t label_index = order[i];
            uint64_t label_name_hash = hrz::murmur3_x64_64(desc->label_names[label_index]);
            uint64_t label_value_hash = hrz::murmur3_x64_64(desc->label_values[label_index]);
            uint64_t label_hash = hrz::hash_mix(label_name_hash, label_value_hash);
            hash = hrz::hash_mix(hash, label_hash);
        }

        // Unlucky!
        if (hash == 0) hash = 0x8080808080808080ull;

        return hash;
    }

    MetricId register_metric(hrz::metrics::MetricDesc* desc, Metric::Type type)
    {
        // Only the global RegiestriesData should be allowed to change this value.
        // Is there a way to enforce this?
        if (desc->inner_id == 0)
        {
            std::unique_lock<std::mutex> lock(_mutex);

            if (desc->hash == 0)
            {
                desc->hash = hash_metric_desc(desc);
                assert(desc->hash != 0);
            }

            auto it = _hash_to_id.find(desc->hash);
            if (it != _hash_to_id.end())
            {
                desc->inner_id = it->second;
            }
            else
            {
                if (type == Metric::Histogram)
                {
                    const auto* histogram = (const hrz::metrics::HistogramDesc*)desc;
                    desc->inner_id = _metric_pool.alloc(Metric(type, histogram));
                }
                else
                {
                    desc->inner_id = _metric_pool.alloc(Metric{type, desc});
                }

                _hash_to_id.insert({desc->hash, desc->inner_id});
            }
        }

        assert(desc->inner_id != 0);
        assert(_metric_pool.get_object(desc->inner_id)->type == type);
        return desc->inner_id;
    }

    void process_operations()
    {
        std::unique_lock<std::mutex> lock(_mutex);

        if (_id_to_operations.empty()) return;

        auto* msgs = PbArena::Create<hrz_monitoring::MonitoringMessages>(&_pb_arena);
        for (auto& pair : _id_to_operations)
        {
            Metric* metric = _metric_pool.get_object(pair.first);
            std::vector<MetricOperation>& operations = pair.second;

            if (operations.empty() || !metric)
            {
                continue;
            }

            // Sort the operations
            std::vector<size_t> order(operations.size());
            for (size_t i = 0; i < order.size(); i++)
            {
                order[i] = i;
            }

            std::sort(
                order.begin(), order.end(),
                [&](size_t a, size_t b)
                { return operations[a].timestamp_us < operations[b].timestamp_us; });

            auto* msg = msgs->add_messages();
            auto* pb_metric = msg->mutable_metric();

            pb_metric->set_name(metric->name);
            for (size_t i = 0; i < metric->label_count; ++i)
            {
                auto* label = pb_metric->add_labels();
                label->set_name(metric->label_names[i]);
                label->set_value(metric->label_values[i]);
            }

            for (auto op_index : order)
            {
                const auto& operation = operations[op_index];
                operation.apply(metric);

                auto* pb_update = pb_metric->add_updates();
                pb_update->set_timestamp(operation.timestamp_us);

                if (metric->type == Metric::Histogram)
                {
                    auto* pb_hist = pb_update->mutable_histogram();
                    const auto& value = std::get<HistogramValue>(metric->value);
                    auto* pb_values = pb_hist->mutable_bucket_values();
                    auto* pb_counts = pb_hist->mutable_bucket_counts();

                    pb_values->Resize(value.bucket_count + 2, 0.0);
                    pb_counts->Resize(value.bucket_count + 2, 0);

                    double increment = (value.max_value - value.min_value) / value.bucket_count;

                    for (size_t i = 0; i < value.bucket_count + 2; ++i)
                    {
                        pb_counts->Set(i, value.buckets[i]);
                        pb_values->Set(i, value.min_value + increment * i);
                    }

                    pb_values->Set(value.bucket_count + 1, (double)INFINITY);
                    pb_hist->set_total_count((int64_t)value.total);
                }
                else if (metric->type == Metric::Counter)
                {
                    pb_update->set_counter((int64_t)std::get<double>(metric->value));
                }
                else if (metric->type == Metric::Gauge)
                {
                    pb_update->set_gauge(std::get<double>(metric->value));
                }
                else
                {
                    assert(!"Unhandled metric type");
                }
            }
            operations.clear();
        }

        hrz_monitoring::push_messages(_buffer, *msgs);
        _pb_arena.Reset();
        _id_to_operations.clear();
    }

    void flush_messages(const std::function<void(const hrz_monitoring::MessageBuffer*)>& callback)
    {
        std::unique_lock<std::mutex> lock(_mutex);

        if (hrz_monitoring::get_written_data(_buffer).empty()) return;

        callback(_buffer);
        hrz_monitoring::reset_buffer(_buffer);
    }
};

struct MainThreadData
{
    bool registries_enabled = false;
};

namespace hrz
{
struct ThreadMetricsRegistry
{
    struct OperationStorage
    {
        std::vector<MetricOperation> operations;
        size_t frame_operation_count;
        bool reset_on_frame;
    };

    bool _enabled = false;
    hrz::flat_hash_map<MetricId, OperationStorage> _id_to_storage;
    hrz::flat_hash_set<MetricId> _touched_this_frame;

    static constexpr int64_t SYNCHRONIZATION_INTERVAL_MS = 250;
    int64_t _last_synchronization_timestamp_ms = 0;

    explicit ThreadMetricsRegistry(bool start_enabled) :
        _enabled(start_enabled), _last_synchronization_timestamp_ms(hrz::now_frame_ms_s64())
    {
    }

    void register_operation(MetricOperation&& op, MetricId id, bool reset_on_frame)
    {
        auto& storage = _id_to_storage[id];
        if (storage.operations.empty() || storage.frame_operation_count == 0
            || !(storage.operations.back().combine_with(&op)))
        {
            storage.operations.push_back(op);
        }
        storage.frame_operation_count++;
        storage.reset_on_frame |= reset_on_frame;

        _touched_this_frame.insert(id);
    }

    void increment_counter(MetricId id, bool reset_on_frame)
    {
        register_operation(
            {MetricOperation::Type::Add, 1.0, hrz::now_us_s64()}, id, reset_on_frame);
    }

    void set_gauge(MetricId id, double value, bool reset_on_frame)
    {
        register_operation(
            {MetricOperation::Type::Set, value, hrz::now_us_s64()}, id, reset_on_frame);
    }

    void add_gauge(MetricId id, double value, bool reset_on_frame)
    {
        register_operation(
            {MetricOperation::Type::Add, value, hrz::now_us_s64()}, id, reset_on_frame);
    }

    void subtract_gauge(MetricId id, double value, bool reset_on_frame)
    {
        register_operation(
            {MetricOperation::Type::Add, -value, hrz::now_us_s64()}, id, reset_on_frame);
    }

    void observe_histogram(MetricId id, double value, bool reset_on_frame)
    {
        register_operation(
            {MetricOperation::Type::ObserveHistogram, value, hrz::now_us_s64()}, id,
            reset_on_frame);
    }

    void finish_frame()
    {
        for (auto& id : _touched_this_frame)
        {
            auto& storage = _id_to_storage.at(id);

            storage.frame_operation_count = 0;
            if (storage.reset_on_frame)
            {
                storage.reset_on_frame = false;
                register_operation(
                    {MetricOperation::Type::Reset, 0.0, hrz::now_us_s64()}, id, false);
            }
        }

        _touched_this_frame.clear();
    }

    void synchronize(SharedData* data)
    {
        int64_t now = hrz::now_frame_ms_s64();
        if (now - _last_synchronization_timestamp_ms > SYNCHRONIZATION_INTERVAL_MS)
        {
            std::unique_lock<std::mutex> lock(data->_mutex);

            _enabled = data->_registries_enabled;
            _last_synchronization_timestamp_ms = now;

            if (!_enabled) return;

            // Since the monitoring buffer is only written to when a data flush is queried,
            // it is the threads that check whether the pending operations occupy too much space
            // and should be cleared when no flushes are queried.
            size_t pending_operations_total_size = 0;

            for (auto& pair : _id_to_storage)
            {
                auto& storage = pair.second;
                if (storage.operations.empty())
                {
                    continue;
                }

                auto& dst = data->_id_to_operations[pair.first];
                std::copy(
                    storage.operations.begin(), storage.operations.end(), std::back_inserter(dst));

                storage.operations.clear();

                pending_operations_total_size += dst.size() * sizeof(MetricOperation);
            }

            if (pending_operations_total_size > MaxBufferedDataSize)
            {
                HRZ_LOG_WARNING("Too much data in metrics operation storage, dropping all of it.");
                data->_id_to_operations.clear();
            }
        }
    }
};

} // namespace hrz

hrz::metrics::MetricDesc::MetricDesc(
    const char* name_,
    bool reset_on_frame_,
    std::initializer_list<std::pair<const char*, std::string>> labels_) :
    inner_id(0),
    name(name_),
    label_count(std::min<size_t>(labels_.size(), 4)),
    reset_on_frame(reset_on_frame_)
{
    size_t i = 0;
    for (const auto& entry : labels_)
    {
        label_names[i] = entry.first;
        label_values[i] = std::move(entry.second);
        i += 1;
        if (i == MAX_LABELS) break;
    }
    assert(i == label_count);
}

void hrz::metrics::MetricDesc::push_label(const char* name, const std::string& value)
{
    if (label_count < MAX_LABELS)
    {
        size_t i = label_count++;
        label_names[i] = name;
        label_values[i] = value;
    }
}

static SharedData g_shared_data;

static thread_local MainThreadData* g_main_thread_data;
static thread_local hrz::ThreadMetricsRegistry* g_thread_registry;

hrz::ThreadMetricsRegistry* hrz::metrics::create_thread_registry(bool enabled)
{
    assert(!g_thread_registry && "This thread's metrics registry is already owned.");

    g_thread_registry = new ThreadMetricsRegistry(enabled);

    std::unique_lock<std::mutex> lock(g_shared_data._mutex);

    return g_thread_registry;
}

void hrz::metrics::destroy_thread_registry(hrz::ThreadMetricsRegistry* registry)
{
    if (g_thread_registry && g_thread_registry == registry)
    {
        delete g_thread_registry;
        g_thread_registry = nullptr;
    }
}

void hrz::metrics::finish_thread_registry_frame()
{
    if (g_thread_registry)
    {
        g_thread_registry->finish_frame();
    }
}

void hrz::metrics::synchronize_thread_registry()
{
    if (g_thread_registry)
    {
        g_thread_registry->synchronize(&g_shared_data);
    }
}

void hrz::metrics::init_main_thread()
{
    std::unique_lock<std::mutex> lock(g_shared_data._mutex);
    assert(!g_shared_data._main_thread_set);

    g_main_thread_data = new MainThreadData();
    g_shared_data._main_thread_set = true;
}

void hrz::metrics::cleanup_main_thread()
{
    assert(g_main_thread_data);

    delete g_main_thread_data;

    std::unique_lock<std::mutex> lock(g_shared_data._mutex);
    g_shared_data._main_thread_set = false;
}

void hrz::metrics::set_metrics_registries_enabled(bool enabled)
{
    assert(g_main_thread_data && "This function should only be called from the main thread.");

    g_main_thread_data->registries_enabled = enabled;

    std::unique_lock<std::mutex> lock(g_shared_data._mutex);
    g_shared_data._registries_enabled = enabled;
}

bool hrz::metrics::are_metrics_registries_enabled()
{
    assert(g_main_thread_data && "This function should only be called from the main thread.");
    return g_main_thread_data->registries_enabled;
}

void hrz::metrics::flush_messages(
    const std::function<void(const hrz_monitoring::MessageBuffer*)>& callback)
{
    assert(g_main_thread_data && "This function should only be called from the main thread.");
    if (!g_main_thread_data->registries_enabled) return;

    g_shared_data.process_operations();
    g_shared_data.flush_messages(callback);
}

void hrz::metrics::increment_counter(MetricDesc* reg)
{
    if (g_thread_registry && g_thread_registry->_enabled)
    {
        MetricId id = g_shared_data.register_metric(reg, Metric::Counter);
        g_thread_registry->increment_counter(id, reg->reset_on_frame);
    }
}

void hrz::metrics::set_gauge(MetricDesc* reg, double value)
{
    if (g_thread_registry && g_thread_registry->_enabled)
    {
        MetricId id = g_shared_data.register_metric(reg, Metric::Gauge);
        g_thread_registry->set_gauge(id, value, reg->reset_on_frame);
    }
}

void hrz::metrics::add_gauge(MetricDesc* reg, double value_to_add)
{
    if (g_thread_registry && g_thread_registry->_enabled)
    {
        MetricId id = g_shared_data.register_metric(reg, Metric::Gauge);
        g_thread_registry->add_gauge(id, value_to_add, reg->reset_on_frame);
    }
}

void hrz::metrics::subtract_gauge(MetricDesc* reg, double value_to_subtract)
{
    if (g_thread_registry && g_thread_registry->_enabled)
    {
        MetricId id = g_shared_data.register_metric(reg, Metric::Gauge);
        g_thread_registry->subtract_gauge(id, value_to_subtract, reg->reset_on_frame);
    }
}

void hrz::metrics::observe_histogram(HistogramDesc* reg, double value)
{
    if (g_thread_registry && g_thread_registry->_enabled)
    {
        MetricId id = g_shared_data.register_metric(reg, Metric::Histogram);
        g_thread_registry->observe_histogram(id, value, reg->reset_on_frame);
    }
}
