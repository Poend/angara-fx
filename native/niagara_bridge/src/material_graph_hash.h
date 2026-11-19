#pragma once
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/templates/hashfuncs.hpp>
#include <godot_cpp/variant/array.hpp>
#include <vector>

namespace godot {
// Hash the function library concurrently with the nodes of a stock, plain-data
// graph. Resource access stays on the caller and all work joins before return.
inline std::vector<uint32_t> graph_element_hashes(const Array &values, int64_t functions_index) {
    const int64_t count = values.size();
    std::vector<uint32_t> result(count);
    WorkerThreadPool *pool = functions_index >= 0 ? WorkerThreadPool::get_singleton() : nullptr;
    struct Job {
        const Variant &value;
        uint32_t &hash;
    };
    if (pool && pool->get_caller_task_id() < 0 && pool->get_caller_group_id() < 0) {
        Job job{values[functions_index], result[functions_index]};
        auto task = pool->add_native_task([](void *data) {
            auto &work = *static_cast<Job *>(data);
            work.hash = work.value.recursive_hash(1);
        }, &job, true);
        for (int64_t i = 0; i < count; ++i)
            if (i != functions_index) result[i] = values[i].recursive_hash(1);
        pool->wait_for_task_completion(task);
    } else {
        for (int64_t i = 0; i < count; ++i) result[i] = values[i].recursive_hash(1);
    }
    return result;
}
inline uint32_t graph_array_hash(const Array &values, int64_t functions_index) {
    if (functions_index < 0) return values.hash();
    uint32_t hash = hash_murmur3_one_32(Variant::ARRAY);
    for (uint32_t part : graph_element_hashes(values, functions_index))
        hash = hash_murmur3_one_32(part, hash);
    return hash_fmix32(hash);
}
} // namespace godot
