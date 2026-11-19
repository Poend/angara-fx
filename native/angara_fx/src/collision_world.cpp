#include "collision_world.h"
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/core/object.hpp>
#include <cmath>
using namespace godot;
namespace {
Vector3 to_world(const Vector3 &cm) {
    return Vector3(cm.y, cm.z, -cm.x) * .01f;
}
Vector3 to_source(const Vector3 &v) {
    return Vector3(-v.z, v.x, v.y);
}
bool whole(const Variant &v, double maximum) {
    return (v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT) && std::isfinite(double(v)) &&
           double(v) >= 0 && double(v) <= maximum && std::floor(double(v)) == double(v);
}
} // namespace
void NBNativeCollisionWorld::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "world", "settings"), &NBNativeCollisionWorld::configure,
                         DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("begin_frame"), &NBNativeCollisionWorld::begin_frame);
    ClassDB::bind_method(D_METHOD("end_frame"), &NBNativeCollisionWorld::end_frame);
    ClassDB::bind_method(D_METHOD("query_sync", "start_cm", "end_cm", "channel", "skip", "complex"),
                         &NBNativeCollisionWorld::query_sync, DEFVAL(0), DEFVAL(false), DEFVAL(false));
    ClassDB::bind_method(D_METHOD("query_async", "interface_key", "previous_id", "start_cm", "end_cm",
                                  "channel", "skip", "complex"),
                         &NBNativeCollisionWorld::query_async, DEFVAL(0), DEFVAL(false), DEFVAL(false));
    ClassDB::bind_method(D_METHOD("get_statistics"), &NBNativeCollisionWorld::get_statistics);
    ClassDB::bind_method(D_METHOD("get_completed_async_queries"), &NBNativeCollisionWorld::get_completed_async_queries);
    ClassDB::bind_method(D_METHOD("get_world"), &NBNativeCollisionWorld::get_world);
}
bool NBNativeCollisionWorld::check_world() {
    error = "";
    if (world.is_null())
        error = "Collision queries require the player's Godot World3D.";
    else if (OS::get_singleton()->get_thread_caller_id() != OS::get_singleton()->get_main_thread_id())
        error = "Godot collision queries must run on the main physics thread.";
    else if (world->get_direct_space_state() == nullptr)
        error = "Godot physics space is unavailable or locked.";
    return error.is_empty();
}
TypedArray<String> NBNativeCollisionWorld::configure(const Ref<World3D> &value, const Dictionary &settings) {
    world.unref();
    batches.clear();
    frame_open = false;
    queries = submitted = hits = frame = 0;
    channel_masks.clear();
    complex_channel_masks.clear();
    exclude.clear();
    world_origin_cm = Vector3();
    TypedArray<String> errors;
    if (String(ProjectSettings::get_singleton()->get_setting("physics/3d/physics_engine", "")) !=
        "Jolt Physics")
        errors.append("Niagara collisions require the Godot Jolt Physics backend.");
    for (const char *name : {"channel_masks", "complex_channel_masks"}) {
        Variant map = settings.get(name, Dictionary());
        if (map.get_type() != Variant::DICTIONARY) {
            errors.append(String(name) + " must be a channel-to-layer dictionary.");
            continue;
        }
        Dictionary source = map, clean;
        Array keys = source.keys();
        for (int i = 0; i < keys.size(); i++) {
            if (!whole(keys[i], 31) || !whole(source[keys[i]], 4294967295.))
                errors.append("Invalid collision channel/mask.");
            else
                clean[int64_t(keys[i])] = int64_t(source[keys[i]]);
        }
        if (String(name) == "channel_masks")
            channel_masks = clean;
        else
            complex_channel_masks = clean;
    }
    Variant origin = settings.get("world_origin_cm", Vector3());
    if (origin.get_type() != Variant::VECTOR3 || !Vector3(origin).is_finite())
        errors.append("Invalid collision world origin.");
    else
        world_origin_cm = origin;
    Variant ignored = settings.get("exclude", Array());
    if (ignored.get_type() != Variant::ARRAY)
        errors.append("Collision exclusions must be an array of RIDs.");
    else {
        Array items = ignored;
        for (int i = 0; i < items.size(); i++) {
            if (items[i].get_type() != Variant::RID)
                errors.append("Invalid excluded physics RID.");
            else
                exclude.append(items[i]);
        }
    }
    if (errors.is_empty()) {
        world = value;
        if (!check_world())
            errors.append(error);
    }
    if (!errors.is_empty()) {
        world.unref();
        error = errors[0];
    } else {
        ray.instantiate();
        ray->set_collide_with_bodies(true);
        ray->set_collide_with_areas(false);
        ray->set_hit_back_faces(true);
        ray->set_hit_from_inside(true);
        ray->set_exclude(exclude);
    }
    return errors;
}
TypedArray<String> NBNativeCollisionWorld::begin_frame() {
    TypedArray<String> errors;
    if (!check_world()) {
        errors.append(error);
        return errors;
    }
    if (frame_open) {
        errors.append("Collision frame already open.");
        return errors;
    }
    for (auto &item : batches) {
        auto &b = item.second;
        b.previous = std::move(b.completed);
        b.completed.clear();
        b.requests.clear();
    }
    frame_open = true;
    frame++;
    return errors;
}
TypedArray<String> NBNativeCollisionWorld::end_frame() {
    TypedArray<String> errors;
    if (!frame_open) {
        errors.append("Collision frame is not open.");
        return errors;
    }
    for (auto &item : batches) {
        auto &b = item.second;
        b.completed.reserve(b.requests.size());
        for (const auto &q : b.requests) {
            Hit hit;
            if (!trace(q.start, q.end, q.channel, false, q.complex, hit)) {
                errors.append(error);
                break;
            }
            b.completed.push_back(hit);
        }
    }
    frame_open = false;
    return errors;
}
bool NBNativeCollisionWorld::trace(const Vector3 &start, const Vector3 &end, int channel, bool skip,
                                   bool complex, Hit &hit) {
    hit = Hit();
    if (!check_world())
        return false;
    if (!start.is_finite() || !end.is_finite() || channel < 0 || channel > 31) {
        error = "Invalid collision ray or source channel.";
        return false;
    }
    if (skip || start.distance_squared_to(end) <= 1e-8f)
        return true;
    if (complex && !complex_channel_masks.has(channel)) {
        error = "TraceComplex requires an explicit channel-to-Godot-collider-layer mapping.";
        return false;
    }
    uint32_t mask = uint32_t(
        int64_t(complex ? complex_channel_masks[channel] : channel_masks.get(channel, int64_t(0xffffffffu))));
    if (mask == 0)
        return true;
    ray->set_from(to_world(start + world_origin_cm));
    ray->set_to(to_world(end + world_origin_cm));
    ray->set_collision_mask(mask);
    ray->set_exclude(exclude);
    Dictionary result;
    TypedArray<RID> filtered;
    bool explicitly_mapped = complex || channel_masks.has(channel);
    for (int attempt = 0;; attempt++) {
        result = world->get_direct_space_state()->intersect_ray(ray);
        queries++;
        if (result.is_empty())
            return true;
        Object *collider = ObjectDB::get_instance(uint64_t(int64_t(result.get("collider_id", 0))));
        if (explicitly_mapped || !collider || !collider->has_meta("niagara_trace_channels"))
            break;
        Variant channels = collider->get_meta("niagara_trace_channels");
        if (!whole(channels, 4294967295.)) {
            error = "Invalid niagara_trace_channels on collision object.";
            return false;
        }
        if (uint32_t(int64_t(channels)) & (uint32_t(1) << channel))
            break;
        if (attempt >= 4095) {
            error = "Collision response filtering exceeded 4096 bodies on one ray.";
            return false;
        }
        if (filtered.is_empty())
            filtered = exclude.duplicate();
        filtered.append(result.get("rid", RID()));
        ray->set_exclude(filtered);
    }
    hit.valid = true;
    hits++;
    Vector3 normal = result.get("normal", Vector3());
    hit.inside = normal.length_squared() == 0;
    // Chaos ray hits starting inside a solid report the opposite trace direction.
    if (hit.inside)
        normal = (to_world(start) - to_world(end)).normalized();
    hit.position = to_source(result.get("position", Vector3())) * 100.f - world_origin_cm;
    hit.normal = to_source(normal);
    RID body = result.get("rid", RID());
    auto *physics = PhysicsServer3D::get_singleton();
    // Godot encodes rough/absorbent combine flags in the coefficient sign.
    hit.friction = std::abs(float(physics->body_get_param(body, PhysicsServer3D::BODY_PARAM_FRICTION)));
    hit.restitution = std::abs(float(physics->body_get_param(body, PhysicsServer3D::BODY_PARAM_BOUNCE)));
    Object *object = ObjectDB::get_instance(uint64_t(int64_t(result.get("collider_id", 0))));
    if (object && object->has_meta("niagara_surface_type")) {
        Variant value = object->get_meta("niagara_surface_type");
        if (!whole(value, 2147483647.)) {
            error = "Invalid niagara_surface_type on collision object.";
            return false;
        }
        hit.surface = int(int64_t(value));
    }
    return true;
}
bool NBNativeCollisionWorld::query_async_native(const String &key, int previous_id, const Vector3 &start,
                                                const Vector3 &end, int channel, bool skip, bool complex,
                                                int &next_id, Hit &hit) {
    hit = Hit();
    next_id = 0;
    if (!check_world())
        return false;
    if (!frame_open) {
        error = "Async collision queries require begin_frame/end_frame.";
        return false;
    }
    if (!start.is_finite() || !end.is_finite() || channel < 0 || channel > 31 ||
        (complex && !skip && !complex_channel_masks.has(channel))) {
        error = "Invalid or unmapped asynchronous collision query.";
        return false;
    }
    auto &batch = batches[key];
    if (!skip && start.distance_squared_to(end) > 1e-8f) {
        if (batch.requests.size() >= 1000000) {
            error = "Async collision request limit exceeded.";
            return false;
        }
        batch.requests.push_back({start, end, channel, complex});
        next_id = int(batch.requests.size());
        submitted++;
    }
    if (previous_id > 0 && size_t(previous_id) <= batch.previous.size())
        hit = batch.previous[previous_id - 1];
    return true;
}
Dictionary NBNativeCollisionWorld::describe(const Hit &hit) const {
    Dictionary result;
    result["valid"] = hit.valid;
    result["inside"] = hit.inside;
    result["position_cm"] = hit.position;
    result["normal"] = hit.normal;
    result["friction"] = hit.friction;
    result["restitution"] = hit.restitution;
    result["surface"] = hit.surface;
    result["error"] = error;
    return result;
}
Array NBNativeCollisionWorld::get_completed_async_queries() const {
    Array result;
    if (frame_open)
        return result;
    for (const auto &entry : batches) {
        const Batch &batch = entry.second;
        for (size_t i = 0; i < batch.completed.size(); ++i) {
            const Request &request = batch.requests[i];
            Dictionary row = describe(batch.completed[i]);
            row["interface"] = entry.first;
            row["query_id"] = int64_t(i + 1);
            row["start_cm"] = request.start;
            row["end_cm"] = request.end;
            row["channel"] = request.channel;
            row["complex"] = request.complex;
            result.append(row);
        }
    }
    return result;
}
Dictionary NBNativeCollisionWorld::query_sync(const Vector3 &start, const Vector3 &end, int channel,
                                              bool skip, bool complex) {
    Hit hit;
    trace(start, end, channel, skip, complex, hit);
    return describe(hit);
}
Dictionary NBNativeCollisionWorld::query_async(const String &key, int previous_id, const Vector3 &start,
                                               const Vector3 &end, int channel, bool skip, bool complex) {
    Hit hit;
    int next = 0;
    query_async_native(key, previous_id, start, end, channel, skip, complex, next, hit);
    Dictionary result = describe(hit);
    result["next_id"] = next;
    return result;
}
Dictionary NBNativeCollisionWorld::get_statistics() const {
    Dictionary result;
    result["native_execution"] = true;
    result["shared_godot_world"] = true;
    result["physics_backend"] = "Jolt Physics";
    result["queries"] = queries;
    result["submitted_async"] = submitted;
    result["hits"] = hits;
    result["frame"] = frame;
    result["frame_open"] = frame_open;
    result["error"] = error;
    return result;
}
