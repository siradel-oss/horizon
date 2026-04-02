#pragma once

#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/monitoring/monitoring.h"

#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace data
{

struct Range
{
    size_t first_index;
    size_t count;
};

struct Metadata
{
    std::string name;
    std::string value;
};

struct Frame
{
    enum FrameRenderType
    {
        VisualRender = 1,
        PickingRender = 2,
        PlanetFeedbackRender = 4
    };

    size_t id;
    int64_t begin;
    int64_t end;
    int render_types;
};

struct SampleId
{
    uint32_t thread;
    size_t tree;  // the index of the sample's tree
    size_t local; // the index of the sample in its tree

    bool operator ==(const SampleId& other) const
    {
        return thread == other.thread && tree == other.tree && local == other.local;
    }
};

struct Sample
{
    SampleId id;
    size_t depth;

    int64_t entry;
    int64_t exit;

    int32_t recursion_max;
    int32_t aggregation_count;
    int64_t aggregation_duration;

    std::string name;
    uint64_t record_hash;

    size_t parent_index;
    size_t first_child_index;
    size_t child_count;
    int64_t time_in_children;

    int64_t duration() const
    {
        return (aggregation_count <= 0) ? exit - entry : aggregation_duration;
    };

    int64_t exclusive_time() const;
};

struct SampleTree
{
    std::vector<Sample> samples;
    size_t id;
};

struct SampleThread
{
    std::vector<SampleTree> trees;
    uint32_t thread_id;

    std::optional<int64_t> min_timestamp;
    std::optional<int64_t> max_timestamp;
    size_t max_depth;
};

struct SampleRecord
{
    std::string name;

    std::string source_file;
    uint32_t source_line;

    int64_t inclusive_time;
    int64_t exclusive_time;

    double average_inclusive_time;
    double average_exclusive_time;

    std::vector<SampleId> associated_samples;
};

struct SampleSystem
{
    std::vector<SampleThread> threads;
    hrz::flat_hash_map<uint64_t, SampleRecord> records;

    size_t sample_count = 0;
    std::optional<int64_t> min_timestamp;
    std::optional<int64_t> max_timestamp;

    const Sample& get_sample(const SampleId& id) const
    {
        return threads.at(id.thread).trees.at(id.tree).samples.at(id.local);
    }

    Range find_trees(int64_t from, int64_t to, uint32_t thread) const;
    std::optional<size_t> find_tree(int64_t timestamp, uint32_t thread) const;

    // Note: the next two mehods use the trees entry timestamps for the comparison
    std::optional<size_t> find_first_tree_before(int64_t timestamp, uint32_t thread) const;
    std::optional<size_t> find_first_tree_after(int64_t timestamp, uint32_t thread) const;

    void add_tree(const hrz_monitoring_proto::Sample&);
    void clear();
};

struct GpuResource
{
    size_t size = 0;
    std::vector<Metadata> metadata;
};

class GpuResourceBucket
{
public:
    std::string layer;
    std::string system;
    std::string type;

    std::span<const GpuResource> get_resources() const { return _resources; }

    size_t get_total_size() const { return _total_size; }

    void push_resource(GpuResource&&);

private:
    std::vector<GpuResource> _resources;
    size_t _total_size = 0;
};

using GpuResourceBucketGroupingFunction = std::function<std::string(const GpuResourceBucket&)>;

struct GpuResourceBucketGroup
{
    std::string key;
    size_t total_size = 0;

    std::vector<const data::GpuResourceBucket*> buckets;
};

std::vector<GpuResourceBucketGroup> group_gpu_resource_buckets(
    std::span<const data::GpuResourceBucket*> buckets,
    const GpuResourceBucketGroupingFunction& grouping_function);

class GpuResourceSnapshot
{
public:
    size_t id;
    int64_t timestamp;

    std::span<const GpuResourceBucket> get_buckets() const { return _buckets; }

    const hrz::flat_hash_set<std::string>& get_known_resource_types() const
    {
        return _known_resource_types;
    }

    const hrz::flat_hash_set<std::string>& get_known_systems() const { return _known_systems; }

    const hrz::flat_hash_set<std::string>& get_known_layers() const { return _known_layers; }

    const hrz::flat_hash_set<std::string>& get_known_metadata() const { return _known_metadata; }

    size_t get_total_size() const { return _total_size; }

    void push_bucket(GpuResourceBucket&&);

private:
    std::vector<GpuResourceBucket> _buckets;
    hrz::flat_hash_set<std::string> _known_resource_types;
    hrz::flat_hash_set<std::string> _known_systems;
    hrz::flat_hash_set<std::string> _known_layers;
    hrz::flat_hash_set<std::string> _known_metadata;
    size_t _total_size = 0;
};

struct Blob
{
    uint64_t id;
    size_t size;
    size_t offset;
    size_t use_count;
    std::vector<Metadata> metadata;
    std::string system;
    std::string layer;
};

struct BlobSnapshot
{
    size_t id;
    int64_t timestamp;

    size_t capacity;
    size_t allocated_blobs_total_size;
    size_t unallocated_blobs_total_size;
    bool is_malloc_passthrough;

    size_t allocated_blobs_count;
    size_t min_blob_offset;
    size_t max_blob_offset;

    std::vector<Blob> allocated_blobs;
    std::vector<Blob> allocated_empty_blobs;
    std::vector<Blob> unallocated_blobs;

    hrz::flat_hash_set<std::string> systems;
    hrz::flat_hash_set<std::string> layers;
    hrz::flat_hash_set<std::string> known_metadata;
};

enum class MetricUnit
{
    None,
    Microsecond,
    Byte
};

const char* metric_unit_label(MetricUnit unit);

struct Metric
{
    std::string name;
    std::map<std::string, std::string> labels;
    std::string display_string;
    MetricUnit unit;
};

bool operator ==(const Metric& lhs, const Metric& rhs);
bool operator !=(const Metric& lhs, const Metric& rhs);

struct MetricHash
{
    std::size_t operator ()(const Metric& metric) const;
};

class Histogram
{
public:
    Histogram(std::span<const double> max_values, std::span<const int64_t> counts);

    struct Bucket
    {
        int64_t count;
        double max_value;
    };

    std::span<const Bucket> get_buckets() const { return _buckets; }

    constexpr size_t get_max_index() const { return _max_index; }

private:
    std::vector<Bucket> _buckets;
    size_t _max_index;
};

template<typename T>
struct MetricUpdate
{
    int64_t timestamp;
    T value;
};

struct MetricUpdateId
{
    Metric metric;
    size_t index;
};

template<typename T>
struct MetricUpdateSystemBase
{
    using Updates = std::vector<MetricUpdate<T>>;

    const MetricUpdate<T>& get_update(const MetricUpdateId& id) const
    {
        return _updates.at(id.metric).at(id.index);
    }

    const hrz::flat_hash_map<Metric, Updates, MetricHash>& get_updates_map() const
    {
        return _updates;
    }

    std::span<const Metric> get_known_metrics() const { return _known_metrics; }

    std::optional<MetricUpdateId> find_update_at(const Metric& metric, int64_t timestamp) const
    {
        auto it = _updates.find(metric);
        if (it == _updates.end())
        {
            return std::nullopt;
        }

        auto updates = it->second;

        // Dichotomic search
        size_t a = 0;
        size_t b = updates.size();

        while (a < b)
        {
            size_t i = (a + b) / 2;
            auto update = updates[i];

            if (update.timestamp > timestamp)
            {
                b = i;
            }
            else if (update.timestamp < timestamp)
            {
                a = i + 1;
            }
            else
            {
                return MetricUpdateId({metric, i});
            }
        }

        // Not found, but there are updates before the timestamp:
        // We want to return the last known value of the metric
        if (b > 0)
        {
            return MetricUpdateId({metric, std::min(a - 1, updates.size() - 1)});
        }

        // Not found because the first update has a morer recent timestamp:
        // We don't know the value of the metric at this time
        return std::nullopt;
    }

    virtual void push_update(Metric&& metric, MetricUpdate<T>&& update)
    {
        bool has_updates = !_updates[metric].empty();

        // Ignore the update if it is outdated
        if (has_updates && _updates[metric].back().timestamp > update.timestamp)
        {
            return;
        }

        _updates[metric].push_back(update);

        if (!has_updates)
        {
            // Register a new metric, but we want to keep the vector sorted
            bool inserted = false;
            for (auto it = _known_metrics.begin(); it != _known_metrics.end(); ++it)
            {
                if (metric.name < it->name)
                {
                    _known_metrics.insert(it, metric);
                    inserted = true;
                    break;
                }
            }

            if (!inserted) _known_metrics.push_back(metric);
        }
    }

    virtual void clear()
    {
        _updates.clear();
        _known_metrics.clear();
    }

private:
    hrz::flat_hash_map<Metric, Updates, MetricHash> _updates;
    std::vector<Metric> _known_metrics;
};

class MetricsSystem : public MetricUpdateSystemBase<double>
{
public:
    double get_maximum(const Metric& metric) const { return _maxima.at(metric); }

    void push_update(Metric&& metric, MetricUpdate<double>&& update) override
    {
        if (!_maxima.contains(metric) || _maxima.at(metric) < update.value)
        {
            _maxima[metric] = update.value;
        }
        MetricUpdateSystemBase<double>::push_update(std::move(metric), std::move(update));
    }

    void clear() override
    {
        _maxima.clear();
        MetricUpdateSystemBase<double>::clear();
    }

private:
    hrz::flat_hash_map<Metric, double, MetricHash> _maxima;
};

using HistogramSystem = MetricUpdateSystemBase<Histogram>;

struct Thread
{
    std::string name;
};

class Database
{
public:
    Database() : _message_buffer(hrz_monitoring::create_buffer()) {}

    ~Database() { hrz_monitoring::destroy_buffer(_message_buffer); }

    std::span<const Frame> get_frames() const { return _frames; }

    std::span<const GpuResourceSnapshot> get_gpu_snapshots() const { return _gpu_snapshots; }

    std::span<const BlobSnapshot> get_blob_snapshots() const { return _blob_snapshots; }

    std::span<const Thread> get_threads() const { return _threads; }

    const SampleSystem& get_samples() const { return _samples; }

    const MetricsSystem& get_metrics() const { return _metrics; }

    const HistogramSystem& get_histograms() const { return _histograms; }

    Range find_frames(int64_t from, int64_t to) const;
    std::optional<size_t> find_last_frame(int64_t timestamp) const;

    const std::string& get_horizon_version() const { return _hrz_version; }

    void clear();
    bool empty() const;

    bool has_unsaved_data() const { return !empty() && _unsaved_data; };

    void register_message(const hrz_monitoring_proto::MonitoringMessage*);
    void process_messages();

    bool save_to_file(const char* filename);
    bool load_from_file(const char* filename);

private:
    std::vector<Frame> _frames;
    std::vector<GpuResourceSnapshot> _gpu_snapshots;
    std::vector<BlobSnapshot> _blob_snapshots;
    std::vector<Thread> _threads;

    SampleSystem _samples;
    MetricsSystem _metrics;
    HistogramSystem _histograms;

    std::string _hrz_version;

    std::vector<hrz_monitoring_proto::MonitoringMessage> _registered_messages;

    hrz_monitoring::MessageBuffer* _message_buffer;

    bool _unsaved_data = false;

    void _process_sample_message(const hrz_monitoring_proto::Sample&);
    void _process_hello_message(const hrz_monitoring_proto::ClientHello&);
    void _process_metric_message(const hrz_monitoring_proto::Metric&);
    void _process_gpu_snapshot_message(const hrz_monitoring_proto::GpuResourcesSnapshot&);
    void _process_frame_message(const hrz_monitoring_proto::Frame&);
    void _process_blob_snapshot_message(const hrz_monitoring_proto::BlobSnapshot&);
    void _process_threads_message(const hrz_monitoring_proto::Threads&);
};

} // namespace data
