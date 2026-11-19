#pragma once
#include "document_values.h"
namespace godot {
class NBNativeWorkspaceDocument : public RefCounted {
    GDCLASS(NBNativeWorkspaceDocument, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static String new_id();
    static Ref<Resource> new_effect(const Dictionary &schemas);
    static Ref<Resource> new_emitter(int64_t index, const Dictionary &schemas);
    static Ref<Resource> new_renderer(const String &type, const Dictionary &schemas);
    static Ref<Resource> load_effect(const String &path, const Ref<Script> &schema);
    static Ref<Resource> load_asset(const String &path);
    static Dictionary save_effect(const Ref<Resource> &effect, String path);
    static Dictionary preserve_session(const Ref<Resource> &effect, const Dictionary &state,
                                       const Dictionary &layout, const String &session_path);
    static Dictionary restore_session(const String &session_path, const String &fallback_recovery,
                                      const Ref<Script> &schema, const Dictionary &memory);
};
} // namespace godot
