#include "data.h"

#include "hrz/fnd/format.h"
#include "hrz/fnd/hash.h"

#include <algorithm>
#include <cassert>
#include <deque>
#include <fstream>
#include <memory>

namespace
{

template<typename T>
using identifier = std::function<int64_t(const T&)>;

template<typename T>
std::optional<size_t> binary_find_at(
    std::span<const T> elements,
    int64_t timestamp,
    const identifier<T>& entry,
    const identifier<T>& exit)
{
    size_t a = 0;
    size_t b = elements.size();

    while (a < b)
    {
        size_t i = (a + b) / 2;
        const auto& element = elements[i];

        if (entry(element) > timestamp)
            b = i;
        else if (exit(element) < timestamp)
            a = i + 1;
        else
            return i;
    }

    return std::nullopt;
}

template<typename T>
std::optional<size_t> binary_find_first_before(
    std::span<const T> elements,
    int64_t timestamp,
    const identifier<T>& entry,
    const identifier<T>& exit)
{
    if (elements.empty()) return std::nullopt;

    size_t a = 0;
    size_t b = elements.size();
    size_t i = 0;

    while (a < b)
    {
        i = (a + b) / 2;
        const auto& element = elements[i];

        if (entry(element) > timestamp)
            b = i;
        else if (
            exit(element) < timestamp && i + 1 < elements.size()
            && entry(elements[i + 1]) < timestamp)
            a = i + 1;
        else
            return i;
    }

    return i;
}

template<typename T>
std::optional<size_t> binary_find_first_after(
    std::span<const T> elements,
    int64_t timestamp,
    const identifier<T>& entry,
    const identifier<T>& exit)
{
    size_t a = 0;
    size_t b = elements.size();

    while (a < b)
    {
        size_t i = (a + b) / 2;
        const auto& element = elements[i];

        if (exit(element) < timestamp)
            a = i + 1;
        else if (i > 0 && entry(elements[i - 1]) > timestamp)
            b = i;
        else
            return i;
    }

    return std::nullopt;
}

template<typename T>
data::Range binary_find_range(
    std::span<const T> elements,
    int64_t from,
    int64_t to,
    const identifier<T>& entry,
    const identifier<T>& exit)
{
    data::Range result = {0, 0};

    auto first_visible = binary_find_at<T>(elements, from, entry, exit);
    if (!first_visible)
    {
        first_visible = binary_find_first_after<T>(elements, from, entry, exit);
        if (!first_visible || entry(elements[*first_visible]) > to)
        {
            return result;
        }
    }

    auto last_visible = binary_find_at<T>(elements, to, entry, exit);
    if (!last_visible)
    {
        last_visible = binary_find_first_before<T>(elements, to, entry, exit);
    }

    result.first_index = *first_visible;
    result.count = *last_visible - *first_visible + 1;

    return result;
}

template<typename MetricMessage>
data::Metric _make_metric(const MetricMessage& metric_message)
{
    data::Metric metric;

    metric.name = metric_message.name();
    metric.display_string = metric_message.name();

    for (auto& label : metric_message.labels())
    {
        metric.labels[label.name()] = label.value();
        metric.display_string += " - " + label.name() + ": " + label.value();
    }

    // Autodetect the unit based on the name
    if (metric.name.substr(metric.name.size() - 4, 4) == "(us)")
    {
        metric.unit = data::MetricUnit::Microsecond;
    }
    else if (metric.name.substr(metric.name.size() - 3, 3) == "(B)")
    {
        metric.unit = data::MetricUnit::Byte;
    }
    else
    {
        metric.unit = data::MetricUnit::None;
    }

    return metric;
}

data::Blob _make_blob(
    const hrz_monitoring_proto::Blob& blob_message,
    const hrz_monitoring_proto::BlobSnapshot& snapshot_message)
{
    data::Blob blob;
    blob.id = blob_message.id();
    blob.size = blob_message.size();
    blob.offset = blob_message.offset();
    blob.use_count = blob_message.use_count();
    blob.system = snapshot_message.systems(blob_message.system());
    blob.layer = snapshot_message.layers(blob_message.layer());

    blob.metadata.reserve(blob_message.metadata_size());
    for (const auto& metadata_message : blob_message.metadata())
    {
        data::Metadata metadata;
        metadata.name = metadata_message.name();
        metadata.value = metadata_message.value();

        blob.metadata.push_back(metadata);
    }

    return blob;
}

size_t _count_samples(const hrz_monitoring_proto::Sample& sample)
{
    size_t count = 0;
    for (const auto& child : sample.children())
    {
        count += _count_samples(child);
    }
    return 1 + count;
}

uint64_t _hash_sample(const hrz_monitoring_proto::Sample& sample)
{
    uint64_t hash = hrz::murmur3_x64_64(sample.name());
    hash = hrz::hash_mix(hash, hrz::murmur3_x64_64(sample.source_file()));
    hash = hrz::hash_mix(hash, (uint64_t)sample.source_line());

    return hash;
}

// Returns an array of pointers, where the i-th element points to the source protobuf message
// used for building the i-th sample in the tree.
// The id of the tree will be used, so make sure it has been set before calling this function.
std::vector<const hrz_monitoring_proto::Sample*> _copy_sample_tree(
    const hrz_monitoring_proto::Sample& src,
    uint32_t thread_id,
    data::SampleTree* tree)
{
    size_t sample_count = _count_samples(src);
    tree->samples.resize(sample_count);

    std::vector<const hrz_monitoring_proto::Sample*> out_sources(sample_count);

    if (sample_count == 0)
    {
        return out_sources; // This should (normally) never happen
    }

    // Locations in memory available for hosting child samples.
    std::span<data::Sample> available_slots(tree->samples.data() + 1, sample_count);
    size_t first_available_slot_index = 1;

    struct SamplePack
    {
        const hrz_monitoring_proto::Sample* src;
        size_t index_in_tree;
        size_t depth;
        size_t parent_index_in_tree;
    };

    std::deque<SamplePack> next_samples;
    next_samples.push_back({&src, 0, 0, 0});

    while (!next_samples.empty())
    {
        const auto* src = next_samples.front().src;
        size_t index_in_tree = next_samples.front().index_in_tree;
        size_t depth = next_samples.front().depth;
        size_t parent_index_in_tree = next_samples.front().parent_index_in_tree;

        out_sources[index_in_tree] = src;

        next_samples.pop_front();

        data::Sample* dst = &tree->samples[index_in_tree];

        dst->id = {thread_id, tree->id, index_in_tree};
        dst->depth = depth;
        dst->entry = src->entry();
        dst->exit = src->exit();
        dst->recursion_max = src->recursion_max();
        dst->aggregation_count = src->aggregation_count();
        dst->aggregation_duration = src->aggregation_time_total();
        dst->name = src->name();
        dst->record_hash = _hash_sample(*src);

        dst->parent_index = parent_index_in_tree;
        dst->first_child_index = first_available_slot_index;
        dst->child_count = src->children_size();

        size_t first_child_index = first_available_slot_index;

        available_slots = available_slots.subspan(src->children_size());
        first_available_slot_index += (size_t)src->children_size();

        for (int i = 0; i < src->children_size(); ++i)
        {
            next_samples.push_back(
                {&src->children(i), first_child_index + i, depth + 1, index_in_tree});

            if (src->children(i).aggregation_count() > 0)
                dst->time_in_children += src->children(i).aggregation_time_total();
            else
                dst->time_in_children += src->children(i).exit() - src->children(i).entry();
        }
    }

    return out_sources;
}

data::SampleRecord _compute_sample_record(
    const data::Sample& sample,
    const hrz_monitoring_proto::Sample& src)
{
    data::SampleRecord record;

    record.name = src.name();
    record.source_file = src.source_file();
    record.source_line = src.source_line();
    record.inclusive_time = sample.duration();
    record.exclusive_time = sample.exclusive_time();
    record.average_inclusive_time = record.inclusive_time;
    record.average_exclusive_time = record.exclusive_time;
    record.associated_samples.push_back(sample.id);

    return record;
}

void _update_sample_record(data::SampleRecord& record, const data::Sample& sample)
{
    record.inclusive_time += sample.duration();
    record.exclusive_time += sample.exclusive_time();
    record.associated_samples.push_back(sample.id);

    const size_t n = record.associated_samples.size();
    record.average_inclusive_time =
        (record.average_inclusive_time * (n - 1) + sample.duration()) / n;
    record.average_exclusive_time =
        (record.average_exclusive_time * (n - 1) + sample.exclusive_time()) / n;
}

void _copy_gpu_snapshot(
    const hrz_monitoring_proto::GpuResourcesSnapshot& src,
    data::GpuResourceSnapshot* snapshot)
{
    snapshot->timestamp = src.timestamp();

    std::vector<size_t> resource_order;
    for (const auto& src_bucket : src.buckets())
    {
        data::GpuResourceBucket bucket;

        bucket.layer = src_bucket.layer();
        bucket.system = src_bucket.system();
        bucket.type = src_bucket.type();

        auto src_resources = src_bucket.resources();

        resource_order.clear();
        for (int i = 0; i < src_resources.size(); ++i)
        {
            resource_order.push_back(i);
        }

        std::sort(
            resource_order.begin(), resource_order.end(),
            [&](size_t a, size_t b) { return src_resources[a].size() > src_resources[b].size(); });

        for (size_t i = 0; i < resource_order.size(); ++i)
        {
            const auto& pb_resource = src_resources[resource_order[i]];
            // Ignore empty resources
            if (pb_resource.size() > 0)
            {
                data::GpuResource resource;
                resource.size = pb_resource.size();
                resource.metadata.resize(pb_resource.metadata_size());
                for (int j = 0; j < pb_resource.metadata_size(); ++j)
                {
                    resource.metadata[j].name = pb_resource.metadata(j).name();
                    resource.metadata[j].value = pb_resource.metadata(j).value();
                }

                bucket.push_resource(std::move(resource));
            }
        }

        // Ignore empty buckets
        if (bucket.get_total_size() > 0)
        {
            snapshot->push_bucket(std::move(bucket));
        }
    }
}

size_t _gpu_buckets_total_size(std::span<const data::GpuResourceBucket*> buckets)
{
    size_t total = 0;
    for (const auto* bucket : buckets)
    {
        total += bucket->get_total_size();
    }
    return total;
}

} // namespace

namespace data
{

int64_t Sample::exclusive_time() const
{
    return duration() - time_in_children;
}

void Database::_process_sample_message(const hrz_monitoring_proto::Sample& sample)
{
    _samples.add_tree(sample);
    _unsaved_data = true;
}

void Database::_process_hello_message(const hrz_monitoring_proto::ClientHello& hello)
{
    _hrz_version = hello.client_version();
}

void Database::_process_metric_message(const hrz_monitoring_proto::Metric& metric_message)
{
    Metric metric = _make_metric(metric_message);
    // A metric without a name makes no sense
    if (metric.name.empty())
    {
        return;
    }

    for (const auto& update_message : metric_message.updates())
    {
        switch (update_message.kind_case())
        {
            case hrz_monitoring_proto::MetricUpdate::kGauge:
            {
                MetricUpdate<double> update;
                update.timestamp = update_message.timestamp();
                update.value = update_message.gauge();
                _metrics.push_update(std::move(metric), std::move(update));

                _unsaved_data = true;
                break;
            }
            case hrz_monitoring_proto::MetricUpdate::kCounter:
            {
                MetricUpdate<double> update;
                update.timestamp = update_message.timestamp();
                update.value = (double)update_message.counter();
                _metrics.push_update(std::move(metric), std::move(update));

                _unsaved_data = true;
                break;
            }
            case hrz_monitoring_proto::MetricUpdate::kHistogram:
            {
                const auto& hist = update_message.histogram();
                auto counts = std::span<const int64_t>(
                    hist.bucket_counts().data(), hist.bucket_counts_size());
                auto values =
                    std::span<const double>(hist.bucket_values().data(), hist.bucket_values_size());
                _histograms.push_update(
                    std::move(metric), {update_message.timestamp(), {values, counts}});

                _unsaved_data = true;
                break;
            }
            default: assert(!"Unhandled metric type"); break;
        }
    }
}

void Database::_process_gpu_snapshot_message(
    const hrz_monitoring_proto::GpuResourcesSnapshot& message)
{
    size_t new_id = _gpu_snapshots.size();
    _gpu_snapshots.push_back({});

    auto* snapshot = &_gpu_snapshots.back();
    snapshot->id = new_id;

    _copy_gpu_snapshot(message, snapshot);
    _unsaved_data = true;
}

void Database::_process_frame_message(const hrz_monitoring_proto::Frame& message)
{
    size_t new_id = _frames.size();
    _frames.push_back({});
    auto& frame = _frames.back();

    frame.id = new_id;
    frame.begin = message.timestamp_begin();
    frame.end = message.timestamp_end();
    frame.render_types = 0;
    if (message.visual_render()) frame.render_types |= Frame::VisualRender;
    if (message.picking_render()) frame.render_types |= Frame::PickingRender;
    if (message.planet_feedback_render()) frame.render_types |= Frame::PlanetFeedbackRender;

    _unsaved_data = true;
}

void Database::_process_blob_snapshot_message(const hrz_monitoring_proto::BlobSnapshot& message)
{
    size_t new_id = _blob_snapshots.size();
    _blob_snapshots.push_back({});
    auto& snapshot = _blob_snapshots.back();

    snapshot.id = new_id;
    snapshot.timestamp = message.timestamp();
    snapshot.is_malloc_passthrough = message.malloc_passthrough();
    snapshot.capacity = message.capacity();

    snapshot.min_blob_offset = snapshot.capacity;
    snapshot.max_blob_offset = 0;

    for (const auto& system : message.systems())
    {
        snapshot.systems.insert(system);
    }

    for (const auto& layer : message.layers())
    {
        snapshot.layers.insert(layer);
    }

    snapshot.allocated_blobs.reserve(message.allocated_blobs_size());
    for (const auto& blob_message : message.allocated_blobs())
    {
        Blob blob = _make_blob(blob_message, message);
        snapshot.allocated_blobs.push_back(blob);
        snapshot.allocated_blobs_total_size += blob.size;
        snapshot.allocated_blobs_count++;

        for (const auto& metadata : blob.metadata)
        {
            snapshot.known_metadata.insert(metadata.name);
        }

        if (blob.offset < snapshot.min_blob_offset) snapshot.min_blob_offset = blob.offset;
        if (blob.offset + blob.size > snapshot.max_blob_offset)
            snapshot.max_blob_offset = blob.offset + blob.size;
    }

    snapshot.allocated_empty_blobs.reserve(message.allocated_empty_blobs_size());
    for (const auto& blob_message : message.allocated_empty_blobs())
    {
        Blob blob = _make_blob(blob_message, message);
        snapshot.allocated_empty_blobs.push_back(blob);
        snapshot.allocated_blobs_count++;

        for (const auto& metadata : blob.metadata)
        {
            snapshot.known_metadata.insert(metadata.name);
        }

        if (blob.offset < snapshot.min_blob_offset) snapshot.min_blob_offset = blob.offset;
        if (blob.offset + blob.size > snapshot.max_blob_offset)
            snapshot.max_blob_offset = blob.offset + blob.size;
    }

    snapshot.unallocated_blobs.reserve(message.unallocated_blobs_size());
    for (const auto& blob_message : message.unallocated_blobs())
    {
        Blob blob = _make_blob(blob_message, message);
        snapshot.unallocated_blobs.push_back(blob);
        snapshot.unallocated_blobs_total_size += blob.size;

        for (const auto& metadata : blob.metadata)
        {
            snapshot.known_metadata.insert(metadata.name);
        }
    }
}

void Database::_process_threads_message(const hrz_monitoring_proto::Threads& message)
{
    for (const auto& thread : message.threads())
    {
        if (thread.id() >= _threads.size())
        {
            _threads.resize(thread.id() + 1);
        }

        _threads[thread.id()].name = thread.name();
    }
}

Range SampleSystem::find_trees(int64_t from, int64_t to, uint32_t thread_id) const
{
    static auto entry = [](const SampleTree& tree) { return tree.samples[0].entry; };
    static auto exit = [](const SampleTree& tree) { return tree.samples[0].exit; };

    return binary_find_range<SampleTree>(threads.at(thread_id).trees, from, to, entry, exit);
}

std::optional<size_t> SampleSystem::find_tree(int64_t timestamp, uint32_t thread_id) const
{
    static auto entry = [](const SampleTree& tree) { return tree.samples[0].entry; };
    static auto exit = [](const SampleTree& tree) { return tree.samples[0].exit; };

    return binary_find_at<SampleTree>(threads.at(thread_id).trees, timestamp, entry, exit);
}

std::optional<size_t> SampleSystem::find_first_tree_before(int64_t timestamp, uint32_t thread_id)
    const
{
    static auto entry = [](const SampleTree& tree) { return tree.samples[0].entry; };
    static auto exit = [](const SampleTree& tree) { return tree.samples[0].exit; };

    return binary_find_first_before<SampleTree>(
        threads.at(thread_id).trees, timestamp, entry, exit);
}

std::optional<size_t> SampleSystem::find_first_tree_after(int64_t timestamp, uint32_t thread_id)
    const
{
    static auto entry = [](const SampleTree& tree) { return tree.samples[0].entry; };
    static auto exit = [](const SampleTree& tree) { return tree.samples[0].exit; };

    return binary_find_first_after<SampleTree>(threads.at(thread_id).trees, timestamp, entry, exit);
}

void SampleSystem::add_tree(const hrz_monitoring_proto::Sample& src)
{
    uint32_t thread_id = src.thread_id();
    if (thread_id >= threads.size())
    {
        threads.resize(thread_id + 1);
    }

    auto& thread = threads.at(thread_id);

    thread.trees.push_back({});
    auto& added_tree = thread.trees.back();
    added_tree.id = thread.trees.size() - 1;

    auto sources = _copy_sample_tree(src, thread_id, &added_tree);

    int64_t tree_min_timestamp = added_tree.samples.front().entry;
    int64_t tree_max_timestamp = added_tree.samples.front().exit;

    if (!thread.min_timestamp.has_value() || tree_min_timestamp < thread.min_timestamp.value())
    {
        thread.min_timestamp = tree_min_timestamp;

        if (!this->min_timestamp.has_value()
            || thread.min_timestamp.value() < this->min_timestamp.value())
        {
            this->min_timestamp = thread.min_timestamp;
        }
    }

    if (!thread.max_timestamp.has_value() || tree_max_timestamp > thread.max_timestamp.value())
    {
        thread.max_timestamp = tree_max_timestamp;

        if (!this->max_timestamp.has_value()
            || thread.max_timestamp.value() > this->max_timestamp.value())
        {
            this->max_timestamp = thread.max_timestamp;
        }
    }

    sample_count += added_tree.samples.size();

    for (size_t i = 0; i < added_tree.samples.size(); ++i)
    {
        const auto& sample = added_tree.samples[i];

        if (sample.depth > thread.max_depth)
        {
            thread.max_depth = sample.depth;
        }

        if (!records.contains(sample.record_hash))
        {
            records[sample.record_hash] = _compute_sample_record(sample, *sources[i]);
        }
        else
        {
            auto& record = records[sample.record_hash];
            _update_sample_record(record, sample);
        }
    }
}

void SampleSystem::clear()
{
    threads.clear();
    records.clear();
    sample_count = 0;
    min_timestamp = std::nullopt;
    max_timestamp = std::nullopt;
}

std::vector<GpuResourceBucketGroup> group_gpu_resource_buckets(
    std::span<const data::GpuResourceBucket*> buckets,
    const GpuResourceBucketGroupingFunction& grouping_function)
{
    std::vector<GpuResourceBucketGroup> result;

    // Find all the different groups using a map
    hrz::flat_hash_map<std::string, std::vector<const data::GpuResourceBucket*>> group_map;
    for (const auto* bucket : buckets)
    {
        auto key = grouping_function(*bucket);
        group_map[key].push_back(bucket);
    }

    // Put the contents of that map into a sorted vector. We can't just rely
    // on the map to handle grouping, as iterating through it will give different
    // a different order every time, which would make displaying this data
    // impossible.
    for (auto& pair : group_map)
    {
        auto& key = pair.first;
        auto& buckets = pair.second;
        result.push_back({key, _gpu_buckets_total_size(buckets), buckets});
    }

    return result;
}

void GpuResourceBucket::push_resource(GpuResource&& resource)
{
    _total_size += resource.size;
    _resources.push_back(resource);
}

void GpuResourceSnapshot::push_bucket(GpuResourceBucket&& bucket)
{
    _total_size += bucket.get_total_size();

    _known_resource_types.insert(bucket.type);
    _known_systems.insert(bucket.system);
    _known_layers.insert(bucket.layer);

    for (const auto& resource : bucket.get_resources())
    {
        for (const auto& metadata : resource.metadata)
        {
            _known_metadata.insert(metadata.name);
        }
    }

    _buckets.push_back(bucket);
}

const char* metric_unit_label(MetricUnit unit)
{
    switch (unit)
    {
        case MetricUnit::Byte: return "B";
        case MetricUnit::Microsecond: return "us";
        default: return "Unspecified";
    }
}

bool operator ==(const Metric& lhs, const Metric& rhs)
{
    if (&lhs == &rhs)
    {
        return true;
    }

    return rhs.labels == lhs.labels && rhs.name == lhs.name;
}

bool operator !=(const Metric& lhs, const Metric& rhs)
{
    return !(lhs == rhs);
}

size_t MetricHash::operator ()(const Metric& metric) const
{
    auto str_hash = std::hash<std::string>{};

    size_t res = str_hash(metric.name);

    for (auto& pair : metric.labels) // The label map is ordered: so the computed hash
                                     // will always remain identical for one set of labels
    {
        res = hrz::hash_mix(hrz::hash_mix(res, str_hash(pair.first)), str_hash(pair.second));
    }

    return res;
}

Histogram::Histogram(std::span<const double> max_values, std::span<const int64_t> counts)
{
    size_t size = std::min(max_values.size(), counts.size());

    _buckets.resize(size);

    _max_index = 0;
    int64_t max_count = 0;

    for (size_t i = 0; i < size; ++i)
    {
        _buckets[i].count = counts[i];
        _buckets[i].max_value = max_values[i];

        if (counts[i] > max_count)
        {
            max_count = counts[i];
            _max_index = (size_t)i;
        }
    }
}

Range Database::find_frames(int64_t from, int64_t to) const
{
    static auto entry = [](const Frame& frame) { return frame.begin; };
    static auto exit = [](const Frame& frame) { return frame.end; };

    return binary_find_range<Frame>(_frames, from, to, entry, exit);
}

std::optional<size_t> Database::find_last_frame(int64_t timestamp) const
{
    static auto entry = [](const Frame& frame) { return frame.begin; };
    static auto exit = [](const Frame& frame) { return frame.end; };

    return binary_find_first_before<Frame>(_frames, timestamp, entry, exit);
}

void Database::clear()
{
    _frames.clear();
    _gpu_snapshots.clear();
    _blob_snapshots.clear();
    _threads.clear();

    _samples.clear();
    _metrics.clear();
    _histograms.clear();

    _hrz_version = "";

    _registered_messages.clear();
    hrz_monitoring::reset_buffer(_message_buffer);
}

bool Database::empty() const
{
    return _frames.empty() && _gpu_snapshots.empty() && _samples.threads.empty()
        && _metrics.get_known_metrics().empty() && _histograms.get_known_metrics().empty();
}

void Database::register_message(const hrz_monitoring_proto::MonitoringMessage* message)
{
    assert(message);

    hrz_monitoring::push_message(_message_buffer, *message);
    _registered_messages.push_back(*message);
}

void Database::process_messages()
{
    for (const auto& message : _registered_messages)
    {
        switch (message.kind_case())
        {
            case hrz_monitoring_proto::MonitoringMessage::kSample:
                _process_sample_message(message.sample());
                break;

            case hrz_monitoring_proto::MonitoringMessage::kClientHello:
                _process_hello_message(message.client_hello());
                break;

            case hrz_monitoring_proto::MonitoringMessage::kMetric:
                _process_metric_message(message.metric());
                break;

            case hrz_monitoring_proto::MonitoringMessage::kGpuResources:
                _process_gpu_snapshot_message(message.gpu_resources());
                break;

            case hrz_monitoring_proto::MonitoringMessage::kFrame:
                _process_frame_message(message.frame());
                break;

            case hrz_monitoring_proto::MonitoringMessage::kBlobs:
                _process_blob_snapshot_message(message.blobs());
                break;

            case hrz_monitoring_proto::MonitoringMessage::kThreads:
                _process_threads_message(message.threads());
                break;

            default:;
        }
    }

    _registered_messages.clear();
}

bool Database::save_to_file(const char* filename)
{
    auto data = hrz_monitoring::get_written_data(_message_buffer);

    std::ofstream ostream(
        filename, std::ios_base::binary | std::ios_base::out | std::ios_base::trunc);
    if (ostream.fail())
    {
        return false;
    }

    ostream.write((const char*)data.data(), data.size());

    if (ostream.good()) _unsaved_data = false;

    return ostream.good();
}

bool Database::load_from_file(const char* filename)
{
    std::ifstream istream(filename, std::ios_base::binary | std::ios_base::in);
    if (istream.fail())
    {
        return false;
    }

    clear();

    size_t written_size;
    constexpr static size_t buffer_size = 4096;
    char buffer[buffer_size];

    std::string parsed_string;

    do
    {
        istream.read(buffer, buffer_size);

        written_size = istream.gcount();
        parsed_string.append(buffer, written_size);
    } while (written_size == buffer_size);

    // Recreate a new message buffer with pre allocated size to avoid heavy string resizes
    hrz_monitoring::destroy_buffer(_message_buffer);
    _message_buffer = hrz_monitoring::create_buffer(parsed_string.size());

    hrz_monitoring::parse_messages(
        {(const std::byte*)parsed_string.c_str(), parsed_string.size()},
        [&](const hrz_monitoring_proto::MonitoringMessage* message) { register_message(message); });

    process_messages();
    _unsaved_data = false;

    return true;
}

} // namespace data
