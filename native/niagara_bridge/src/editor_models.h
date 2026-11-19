#pragma once
#include "document_values.h"
#include <godot_cpp/classes/hashing_context.hpp>
namespace godot {
class NBNativeEditorModels : public RefCounted {
    GDCLASS(NBNativeEditorModels, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static String compute(const Ref<Resource> &resource);
    static void token(const Ref<HashingContext> &digest, const String &value);
    static void feed(const Variant &value, const Ref<HashingContext> &digest, const Dictionary &ancestors);
    static bool plain_container(const Variant &value);
    static String project_path(const Variant &value);
    static Dictionary read_bundle(const String &path, const Ref<Script> &plan_schema);
    static Dictionary read_inventory(const Dictionary &state, const String &project);
    static String registry_path(const String &primary, const String &fallback);
};
} // namespace godot
