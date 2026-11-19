#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
namespace godot {
class NBNativeDocumentCopy : public RefCounted {
    GDCLASS(NBNativeDocumentCopy, RefCounted)
  protected:
    static void _bind_methods();

  public:
    // The script facade supplies a fresh dictionary for ordinary calls, and may
    // pass one shared map explicitly when copying related authored roots.
    static Variant copy(const Variant &value, bool renew_ids, Dictionary seen);
};
} // namespace godot
