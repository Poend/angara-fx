#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <map>
#include <vector>

namespace godot {
// One context per effect instance, using its existing Godot physics space.
// Async IDs are one-based slots in the previous frame, separately for each DI.
class NBNativeCollisionWorld : public RefCounted {
    GDCLASS(NBNativeCollisionWorld, RefCounted)
  public:
    struct Hit {
        bool valid = false, inside = false;
        Vector3 position, normal;
        float friction = 0, restitution = 0;
        int32_t surface = 0;
    };

  private:
    struct Request {
        Vector3 start, end;
        int channel = 0;
        bool complex = false;
    };
    struct Batch {
        std::vector<Request> requests;
        std::vector<Hit> previous, completed;
    };
    Ref<World3D> world;
    Ref<PhysicsRayQueryParameters3D> ray;
    Dictionary channel_masks, complex_channel_masks;
    TypedArray<RID> exclude;
    Vector3 world_origin_cm;
    std::map<String, Batch> batches;
    String error;
    bool frame_open = false;
    int64_t queries = 0, submitted = 0, hits = 0, frame = 0;
    bool check_world();
    Dictionary describe(const Hit &hit) const;

  protected:
    static void _bind_methods();

  public:
    TypedArray<String> configure(const Ref<World3D> &value, const Dictionary &settings = Dictionary());
    TypedArray<String> begin_frame();
    TypedArray<String> end_frame();
    bool trace(const Vector3 &start, const Vector3 &end, int channel, bool skip, bool complex, Hit &hit);
    bool query_async_native(const String &key, int previous_id, const Vector3 &start, const Vector3 &end,
                            int channel, bool skip, bool complex, int &next_id, Hit &hit);
    Dictionary query_sync(const Vector3 &start, const Vector3 &end, int channel = 0, bool skip = false,
                          bool complex = false);
    Dictionary query_async(const String &key, int previous_id, const Vector3 &start, const Vector3 &end,
                           int channel = 0, bool skip = false, bool complex = false);
    Dictionary get_statistics() const;
    Array get_completed_async_queries() const;
    Ref<World3D> get_world() const {
        return world;
    }
    String get_error() const {
        return error;
    }
};
} // namespace godot
