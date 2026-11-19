#pragma once
#include "document_values.h"
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/callable.hpp>
namespace godot {
class NBNativeEditorActions : public RefCounted {
  GDCLASS(NBNativeEditorActions, RefCounted)
protected:
  static void _bind_methods();

public:
  static TypedArray<Dictionary> catalog(const TypedArray<Dictionary> &entries,
                                        const String &stage);
  static Ref<Resource> create_module(const TypedArray<Dictionary> &entries,
                                     const String &name,
                                     const Ref<Script> &module_schema,
                                     const Ref<Script> &input_schema);
  static String validate_module_stage(const TypedArray<Dictionary> &entries,
                                      const Ref<Resource> &module,
                                      const String &stage,
                                      const String &original);
  static Dictionary capture_selection(const TypedArray<Resource> &selected,
                                      const Ref<Resource> &primary,
                                      const Ref<Resource> &stage,
                                      const Ref<Resource> &module);
  static Dictionary resolve_selection(const Ref<Resource> &effect,
                                      const Dictionary &selection);
  static Dictionary validate_selection(const Ref<Resource> &effect,
                                       const Dictionary &selection);
  static Ref<Resource> owner_of_stage(const Ref<Resource> &effect,
                                      const Ref<Resource> &stage);
  static Ref<Resource> selected_module_stage(const Ref<Resource> &owner,
                                             const Ref<Resource> &module);
  static bool contains_renderer(const Ref<Resource> &effect,
                                const Ref<Resource> &renderer);
  static int64_t material_slot_count(const Ref<Resource> &renderer);
  static TypedArray<Resource> user_entries(const Ref<Resource> &effect);
  static Variant edited_value(const Variant &original, const Array &path,
                              const Variant &value);
  static Dictionary differences(const Ref<Resource> &target,
                                const Dictionary &changes);
  static bool edit_properties(Object *history, const Ref<Resource> &target,
                              const Dictionary &changes, const String &label,
                              const Callable &changed);
  static Dictionary move_module(Object *history, const Ref<Resource> &source,
                                const Ref<Resource> &target,
                                const Ref<Resource> &module, int64_t index,
                                const Callable &changed);
  static Dictionary duplicate_emitters(const Ref<Resource> &effect,
                                       const TypedArray<Resource> &selected);
};
} // namespace godot
