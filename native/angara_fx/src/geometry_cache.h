#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <mutex>
namespace godot {
class NBNativeGeometryCache : public RefCounted {
    GDCLASS(NBNativeGeometryCache, RefCounted)
    std::recursive_mutex lock;
    Dictionary cache, prepared_arrays;
    static Ref<NBNativeGeometryCache> singleton;
    static std::mutex singleton_lock;
    static Ref<NBNativeGeometryCache> instance();
    Dictionary cached_packet(const Ref<Mesh> &mesh, int64_t surface);
    void invalidate_mesh(int64_t identity);
    void clear();

  protected:
    static void _bind_methods();

  public:
    static Dictionary packet(const Ref<Mesh> &mesh, int64_t surface);
    static Dictionary build(const Ref<Mesh> &mesh, int64_t surface);
    static void seed_composite(const Ref<Mesh> &mesh,const Array &arrays);
    static void invalidate(int64_t identity);
    static Dictionary entries();
    static void shutdown();
};
} // namespace godot
