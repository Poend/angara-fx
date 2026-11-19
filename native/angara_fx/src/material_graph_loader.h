#pragma once
#include "document_values.h"
namespace godot {
class NBNativeT3DParser : public RefCounted {
    GDCLASS(NBNativeT3DParser, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary parse(const String &text);
    static Dictionary parse_file(const String &path);
    static String object_path(const String &reference);
};
class NBNativeMaterialGraphLoader : public RefCounted {
    GDCLASS(NBNativeMaterialGraphLoader, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Ref<Resource> load_graph(const String &manifest_path, const String &material_path,
                                    const Ref<Script> &graph_schema);
    static Dictionary load_function(const Ref<Resource> &graph, const String &source_path);
    static Dictionary load_document(const Ref<Resource> &graph, const String &path);
    static Dictionary normalize(const Dictionary &document, const Ref<Resource> &graph);
    static Dictionary pin(const Variant &raw, const Dictionary &document, const Ref<Resource> &graph);
    static void apply_instance(const Ref<Resource> &graph, const Dictionary &root);
    static String asset_path(const String &value);
    static String project_path(const String &value);
    static String manifest_key(const Dictionary &records, const String &path);
};
} // namespace godot
