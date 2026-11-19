#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
namespace godot {
class NBNativeVectorFields : public RefCounted {
    GDCLASS(NBNativeVectorFields, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static String validation_error(const Ref<Resource> &field);
    static Vector3 sample_cpu(const Ref<Resource> &field, const Vector3 &position,
                              const Vector3i &tiling = Vector3i(1, 1, 1));
    static int64_t index(double value, int64_t dimension, bool tiled);
    static double lerp(double a, double b, double weight);
    static double value(const Ref<Resource> &field, int64_t x, int64_t y, int64_t z, int64_t component);
};
class NBNativeAttractionForces : public RefCounted {
    GDCLASS(NBNativeAttractionForces, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Variant input(const Ref<Resource> &operation, const String &key, const Variant &fallback,
                         const Dictionary &context);
    static Vector3 vector(const Ref<Resource> &operation, const String &key, const Variant &fallback,
                          const Dictionary &context);
    static Dictionary reference(const String &name);
    static double normalized_range(double distance, double radius);
    static Vector3 vortex(const Ref<Resource> &operation, const Dictionary &context);
    static Vector3 line(const Ref<Resource> &operation, const Dictionary &context);
};
class NBNativeCurlNoise : public RefCounted {
    GDCLASS(NBNativeCurlNoise, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static int64_t quality(const Ref<Resource> &operation);
    static Variant input(const Ref<Resource> &operation, const String &key, const Variant &fallback,
                         const Dictionary &context);
    static double ordinary_phase(const Ref<Resource> &operation, int64_t seed);
    static bool valid_seed(const Variant &value);
    static Vector3 force(const Ref<Resource> &operation, Dictionary context);
    static void clear_cache();
};
} // namespace godot
