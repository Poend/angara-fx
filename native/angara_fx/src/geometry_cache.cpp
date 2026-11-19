#include "geometry_cache.h"
#include "raster_buffers.h"
#include <godot_cpp/variant/utility_functions.hpp>
namespace godot {
Ref<NBNativeGeometryCache> NBNativeGeometryCache::singleton;
std::mutex NBNativeGeometryCache::singleton_lock;
Ref<NBNativeGeometryCache> NBNativeGeometryCache::instance() {
    std::lock_guard<std::mutex> guard(singleton_lock);
    if (singleton.is_null())
        singleton.instantiate();
    return singleton;
}
void NBNativeGeometryCache::_bind_methods() {
    ClassDB::bind_static_method("NBNativeGeometryCache", D_METHOD("packet", "mesh", "surface"),
                                &NBNativeGeometryCache::packet);
    ClassDB::bind_static_method("NBNativeGeometryCache", D_METHOD("build", "mesh", "surface"),
                                &NBNativeGeometryCache::build);
    ClassDB::bind_static_method("NBNativeGeometryCache", D_METHOD("invalidate", "identity"),
                                &NBNativeGeometryCache::invalidate);
    ClassDB::bind_static_method("NBNativeGeometryCache", D_METHOD("entries"),
                                &NBNativeGeometryCache::entries);
    ClassDB::bind_method(D_METHOD("_invalidate", "identity"), &NBNativeGeometryCache::invalidate_mesh);
}
Dictionary NBNativeGeometryCache::build(const Ref<Mesh> &mesh, int64_t surface) {
    Dictionary result;
    Array arrays = mesh.is_valid() ? mesh->surface_get_arrays(surface) : Array();
    PackedInt32Array indices;
    PackedByteArray geometry;
    if (!arrays.is_empty()) {
        if (arrays[Mesh::ARRAY_INDEX].get_type() != Variant::NIL)
            indices = arrays[Mesh::ARRAY_INDEX];
        if (indices.is_empty()) {
            PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
            indices.resize(vertices.size());
            for (int n = 0; n < vertices.size(); ++n)
                indices.set(n, n);
        }
        geometry = NBNativeRasterBuffers::geometry_bytes(arrays);
    }
    result["arrays"] = arrays;
    result["indices"] = indices;
    result["geometry"] = geometry;
    return result;
}
Dictionary NBNativeGeometryCache::packet(const Ref<Mesh> &mesh, int64_t surface) {
    if (mesh.is_null() || !bool(mesh->get_meta("angara_fx_immutable_geometry", false)))
        return build(mesh, surface);
    return instance()->cached_packet(mesh, surface);
}
void NBNativeGeometryCache::seed_composite(const Ref<Mesh> &mesh,const Array &arrays) {
    if(mesh.is_null() || arrays.size()!=Mesh::ARRAY_MAX) return;
    Ref<NBNativeGeometryCache> owner=instance();
    // Register weak ownership and changed invalidation without requesting the
    // just-uploaded RenderingServer surface. Reuse the normal cache entry path.
    std::lock_guard<std::recursive_mutex> guard(owner->lock);
    int64_t identity=mesh->get_instance_id();
    if(!owner->cache.has(identity)) {
        if(owner->cache.size()>=256) return; // generic lazy path handles eviction
        Callable callback=Callable(owner.ptr(),"_invalidate").bind(identity);
        if(!mesh->is_connected("changed",callback)) mesh->connect("changed",callback);
        Dictionary entry;entry["mesh"]=UtilityFunctions::weakref(mesh);entry["callback"]=callback;entry["surfaces"]=Dictionary();
        owner->cache[identity]=entry;
    }
    // Most spatial-only effects never request a raster packet. Retain COW
    // arrays cheaply and quantize only for the first actual raster consumer.
    owner->prepared_arrays[identity]=arrays;
    Dictionary surfaces=Dictionary(owner->cache[identity])["surfaces"];
    surfaces.erase(0);
}
Dictionary NBNativeGeometryCache::cached_packet(const Ref<Mesh> &mesh, int64_t surface) {
    std::lock_guard<std::recursive_mutex> guard(lock);
    int64_t identity = mesh->get_instance_id();
    bool matches = false;
    if (cache.has(identity)) {
        Ref<RefCounted> weak = Dictionary(cache[identity])["mesh"];
        matches = Ref<Mesh>(weak->call("get_ref")) == mesh;
    }
    if (!matches) {
        if (cache.size() >= 256) {
            Array keys = cache.keys();
            for (int n = 0; n < keys.size(); ++n) {
                Ref<RefCounted> weak = Dictionary(cache[keys[n]])["mesh"];
                if (Variant(weak->call("get_ref")).get_validated_object() == nullptr)
                    {prepared_arrays.erase(keys[n]);cache.erase(keys[n]);}
            }
            if (cache.size() >= 256) {
                Variant key = cache.keys()[0];
                Dictionary entry = cache[key];
                Ref<RefCounted> weak = entry["mesh"];
                Ref<Mesh> old = weak->call("get_ref");
                Callable callback = entry["callback"];
                if (old.is_valid() && old->is_connected("changed", callback))
                    old->disconnect("changed", callback);
                prepared_arrays.erase(key);
                cache.erase(key);
            }
        }
        Callable callback = Callable(this, "_invalidate").bind(identity);
        mesh->connect("changed", callback);
        Dictionary entry;
        entry["mesh"] = UtilityFunctions::weakref(mesh);
        entry["callback"] = callback;
        entry["surfaces"] = Dictionary();
        cache[identity] = entry;
    }
    Dictionary surfaces = Dictionary(cache[identity])["surfaces"];
    if (!surfaces.has(surface)) {
        if(surface==0 && prepared_arrays.has(identity)) {
            Array decoded=NBNativeRasterBuffers::quantized_geometry_arrays(prepared_arrays[identity]);
            if(decoded.is_empty()) surfaces[surface]=build(mesh,surface);
            else {
                Dictionary packet;packet["arrays"]=decoded;packet["indices"]=decoded[Mesh::ARRAY_INDEX];
                packet["geometry"]=NBNativeRasterBuffers::geometry_bytes(decoded);
                surfaces[surface]=packet;
            }
            prepared_arrays.erase(identity);
        } else surfaces[surface] = build(mesh, surface);
    }
    return surfaces[surface];
}
void NBNativeGeometryCache::invalidate_mesh(int64_t identity) {
    std::lock_guard<std::recursive_mutex> guard(lock);
    prepared_arrays.erase(identity);
    if (cache.has(identity)) {
        Dictionary surfaces = Dictionary(cache[identity])["surfaces"];
        surfaces.clear();
    }
}
void NBNativeGeometryCache::invalidate(int64_t identity) {
    instance()->invalidate_mesh(identity);
}
Dictionary NBNativeGeometryCache::entries() {
    Ref<NBNativeGeometryCache> value = instance();
    std::lock_guard<std::recursive_mutex> guard(value->lock);
    return value->cache.duplicate();
}
void NBNativeGeometryCache::clear() {
    std::lock_guard<std::recursive_mutex> guard(lock);
    Array values = cache.values();
    for (int n = 0; n < values.size(); ++n) {
        Dictionary entry = values[n];
        Ref<RefCounted> weak = entry["mesh"];
        Ref<Mesh> mesh = weak->call("get_ref");
        Callable callback = entry["callback"];
        if (mesh.is_valid() && mesh->is_connected("changed", callback))
            mesh->disconnect("changed", callback);
    }
    cache.clear();
    prepared_arrays.clear();
}
void NBNativeGeometryCache::shutdown() {
    Ref<NBNativeGeometryCache> value;
    {
        std::lock_guard<std::mutex> guard(singleton_lock);
        value = singleton;
        singleton.unref();
    }
    if (value.is_valid())
        value->clear();
}
} // namespace godot
