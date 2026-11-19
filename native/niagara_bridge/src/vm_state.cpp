#include "vm_program.h"
#include "vm_scalar.h"
#include <godot_cpp/core/class_db.hpp>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <cmath>
using namespace godot;
static bool uint_word(const Variant &v) {
    if (v.get_type() != Variant::INT && v.get_type() != Variant::FLOAT)
        return false;
    double d = v;
    return std::isfinite(d) && d >= 0 && d <= 4294967295. && d == std::floor(d);
}
void NBNativeVMRandom::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "words", "counters"), &NBNativeVMRandom::configure);
    ClassDB::bind_method(D_METHOD("initialize_from_entropy", "cycles", "second", "nonce"),
                         &NBNativeVMRandom::initialize_from_entropy);
    ClassDB::bind_method(D_METHOD("initialize_runtime"), &NBNativeVMRandom::initialize_runtime);
    ClassDB::bind_method(D_METHOD("next_uint4"), &NBNativeVMRandom::next_uint4);
    ClassDB::bind_method(D_METHOD("next_fraction4"), &NBNativeVMRandom::next_fraction4);
    ClassDB::bind_method(D_METHOD("get_words"), &NBNativeVMRandom::get_words);
    ClassDB::bind_method(D_METHOD("get_counters"), &NBNativeVMRandom::get_counters);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "words", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_words");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "counters", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_counters");
}
bool NBNativeVMRandom::configure(const Array &initial, const Array &counts) {
    ready = false;
    if (initial.size() != 20 || counts.size() != 4)
        return false;
    for (int i = 0; i < 20; i++)
        if (!uint_word(initial[i]))
            return false;
    for (int i = 0; i < 4; i++)
        if (!uint_word(counts[i]))
            return false;
    for (int i = 0; i < 20; i++)
        words[i] = uint32_t(int64_t(initial[i]));
    for (int i = 0; i < 4; i++)
        counters[i] = uint32_t(int64_t(counts[i]));
    return ready = true;
}
bool NBNativeVMRandom::initialize_from_entropy(const Array &cycles, int64_t second, int64_t nonce) {
    if (cycles.size() != 2 || !uint_word(cycles[0]) || !uint_word(cycles[1]) || second < 0 ||
        uint64_t(second) > UINT32_MAX || nonce < 0 || uint64_t(nonce) > UINT32_MAX)
        return false;
    uint64_t state =
        uint32_t(int64_t(cycles[0])) | (uint64_t(uint32_t(int64_t(cycles[1])) ^ uint32_t(second)) << 32);
    uint64_t increment = UINT64_C(0xd15ea5e5) | (uint64_t(uint32_t(nonce) ^ 0xcafef00d) << 32);
    for (auto &word : words) {
        uint64_t old = state;
        state = old * UINT64_C(0x5851f42d4c957f2d) + increment;
        uint32_t shifted = uint32_t(((old >> 18) ^ old) >> 27), rotation = uint32_t(old >> 59);
        word = (shifted >> rotation) | (shifted << ((-rotation) & 31));
    }
    counters = {uint32_t(increment), uint32_t(increment >> 32), uint32_t(state), uint32_t(state >> 32)};
    return ready = true;
}
bool NBNativeVMRandom::initialize_runtime() {
    static std::atomic<uint64_t> nonce{1};
    uint64_t tick = uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()),
             n = nonce.fetch_add(1);
    Array cycles;
    cycles.append(int64_t(uint32_t(tick)));
    cycles.append(int64_t(uint32_t(tick >> 32)));
    return initialize_from_entropy(cycles, uint32_t(tick ^ n), uint32_t(n * 0x9e3779b9) & 0xfffffff0);
}
bool NBNativeVMRandom::next(std::array<uint32_t, 4> &result) {
    if (!ready)
        return false;
    for (int lane = 0; lane < 4; lane++) {
        uint32_t t = words[16 + lane], s = words[lane];
        for (int i = 4; i > 0; i--)
            words[i * 4 + lane] = words[(i - 1) * 4 + lane];
        t ^= t >> 2;
        t ^= t << 1;
        t ^= s << 4;
        words[lane] = t;
        counters[lane] += 362437;
        result[lane] = t + counters[lane];
    }
    return true;
}
PackedInt64Array NBNativeVMRandom::next_uint4() {
    std::array<uint32_t, 4> values{};
    PackedInt64Array out;
    if (!next(values))
        return out;
    for (auto v : values)
        out.append(v);
    return out;
}
Vector4 NBNativeVMRandom::next_fraction4() {
    std::array<uint32_t, 4> values{};
    if (!next(values))
        return Vector4();
    return Vector4(float(values[0] >> 9) / 8388608.f, float(values[1] >> 9) / 8388608.f,
                   float(values[2] >> 9) / 8388608.f, float(values[3] >> 9) / 8388608.f);
}
Array NBNativeVMRandom::get_words() const {
    Array a;
    for (auto w : words)
        a.append(int64_t(w));
    return a;
}
Array NBNativeVMRandom::get_counters() const {
    Array a;
    for (auto w : counters)
        a.append(int64_t(w));
    return a;
}
void NBNativeVMIDs::_bind_methods() {
    ClassDB::bind_method(D_METHOD("allocate", "required", "tag", "maintain"), &NBNativeVMIDs::allocate,
                         DEFVAL(false));
    ClassDB::bind_method(D_METHOD("acquire", "count"), &NBNativeVMIDs::acquire);
    ClassDB::bind_method(D_METHOD("update", "ids", "kept", "output_start"), &NBNativeVMIDs::update);
    ClassDB::bind_method(D_METHOD("snapshot"), &NBNativeVMIDs::snapshot);
    ClassDB::bind_method(D_METHOD("restore", "snapshot"), &NBNativeVMIDs::restore);
    ClassDB::bind_method(D_METHOD("get_acquire_tag"), &NBNativeVMIDs::get_acquire_tag);
    ClassDB::bind_method(D_METHOD("get_free"), &NBNativeVMIDs::get_free);
    ClassDB::bind_method(D_METHOD("get_table"), &NBNativeVMIDs::get_table);
    ClassDB::bind_method(D_METHOD("get_spawned"), &NBNativeVMIDs::get_spawned);
    ClassDB::bind_method(D_METHOD("get_max_used"), &NBNativeVMIDs::get_max_used);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "acquire_tag", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_acquire_tag");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "free", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_free");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "table", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_table");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "spawned", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_spawned");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_used", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_max_used");
}
void NBNativeVMIDs::allocate(int required, int tag, bool maintain) {
    if (required < 0 || required > 10000000)
        return;
    const int old = int(table.size());
    for (int id = required - 1; id >= old; id--)
        free_ids.push_back(id);
    if (required > old)
        table.resize(required, -1);
    if (!maintain) {
        std::fill(table.begin(), table.end(), -1);
        max_used = -1;
        spawned.clear();
    }
    acquire_tag = tag;
}
TypedArray<String> NBNativeVMIDs::restore(const Dictionary &state) {
    TypedArray<String> errors;
    std::vector<int32_t> new_table, new_free, new_spawned;
    auto parse = [&](const char *key, std::vector<int32_t> &out, bool positions) {
        Variant raw = state.get(key, Array());
        if (raw.get_type() != Variant::ARRAY) {
            errors.append("Invalid persistent ID snapshot array.");
            return;
        }
        Array values = raw;
        if (values.size() > 10000000) {
            errors.append("Persistent ID snapshot exceeds supported size.");
            return;
        }
        for (int n = 0; n < values.size(); n++) {
            Variant value = values[n];
            if ((value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) ||
                !std::isfinite(double(value)) || double(value) != std::floor(double(value)) ||
                double(value) < (positions ? -1. : 0.) || double(value) > INT32_MAX) {
                errors.append("Invalid persistent ID snapshot value.");
                return;
            }
            out.push_back(int32_t(int64_t(value)));
        }
    };
    if (!state.has("table") || !state.has("free"))
        errors.append("Missing persistent ID snapshot tables.");
    parse("table", new_table, true);
    parse("free", new_free, false);
    parse("spawned", new_spawned, false);
    for (int id : new_free)
        if (size_t(id) >= new_table.size())
            errors.append("Free ID exceeds snapshot table.");
    for (int id : new_spawned)
        if (size_t(id) >= new_table.size())
            errors.append("Spawned ID exceeds snapshot table.");
    for (const char *key : {"acquire_tag", "max_used"}) {
        Variant value = state.get(key, Variant());
        if ((value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) ||
            !std::isfinite(double(value)) || double(value) != std::floor(double(value)) ||
            double(value) < INT32_MIN || double(value) > INT32_MAX)
            errors.append("Invalid ID snapshot metadata.");
    }
    if (!errors.is_empty())
        return errors;
    table = std::move(new_table);
    free_ids = std::move(new_free);
    spawned = std::move(new_spawned);
    acquire_tag = int32_t(int64_t(state["acquire_tag"]));
    max_used = int32_t(int64_t(state["max_used"]));
    return errors;
}
Dictionary NBNativeVMIDs::acquire(int count) {
    Dictionary result;
    TypedArray<String> problems;
    Array ids;
    if (count < 0 || size_t(count) > free_ids.size())
        problems.append("Persistent ID free table exhausted.");
    else {
        std::vector<int32_t> block;
        block.reserve(count);
        for (int i = 0; i < count; i++) {
            int id = free_ids.back();
            free_ids.pop_back();
            ids.append(id);
            block.push_back(id);
        }
        spawned.insert(spawned.begin(), block.begin(), block.end());
    }
    result["errors"] = problems;
    result["ids"] = ids;
    return result;
}
TypedArray<String> NBNativeVMIDs::update(const Array &ids, const Array &kept, int output_start) {
    TypedArray<String> problems;
    for (int i = 0; i < ids.size(); i++) {
        int64_t id = ids[i];
        if (!uint_word(ids[i]) || id >= int64_t(table.size()))
            problems.append("Persistent ID outside allocated table.");
    }
    if (!problems.is_empty())
        return problems;
    int out = 0;
    for (int lane = 0; lane < ids.size(); lane++) {
        int id = int64_t(ids[lane]);
        if (out < kept.size() && int64_t(kept[out]) == lane) {
            table[id] = output_start + out;
            out++;
        } else
            free_ids.push_back(id);
    }
    for (int lane = 0; lane < kept.size(); lane++)
        max_used = std::max(max_used, int32_t(int64_t(ids[lane])));
    return problems;
}
Dictionary NBNativeVMIDs::snapshot() const {
    Dictionary d;
    Array a, b, c;
    for (auto i : table)
        a.append(i);
    for (auto i : free_ids)
        b.append(i);
    for (auto i : spawned)
        c.append(i);
    d["table"] = a;
    d["free"] = b;
    d["spawned"] = c;
    d["acquire_tag"] = acquire_tag;
    d["max_used"] = max_used;
    return d;
}
void NBNativeVMProgram::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_definition", "value"), &NBNativeVMProgram::set_definition);
    ClassDB::bind_method(D_METHOD("get_definition"), &NBNativeVMProgram::get_definition);
    ClassDB::bind_method(D_METHOD("validate"), &NBNativeVMProgram::validate);
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "definition"), "set_definition", "get_definition");
}
TypedArray<String> NBNativeVMProgram::validate() const {
    return NBNativeVMExecutor::validate_program(definition);
}
