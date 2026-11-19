#pragma once
#include "material_graph_loader.h"
#include "texture_builder.h"
#include <vector>
namespace godot {
class NBNativeMaterialCompiler : public RefCounted {
    GDCLASS(NBNativeMaterialCompiler, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary compile(const Ref<Resource> &graph, const Dictionary &textures,
                              const Dictionary &options, Dictionary trace);
    static Dictionary support_for_features(const Dictionary &features, bool batch);
    static bool verified_unbiased_full_chain(const Dictionary &descriptor, const Ref<Texture2D> &texture);
};
class NBNativeCustomArithmetic : public RefCounted {
    GDCLASS(NBNativeCustomArithmetic, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary compile(const String &source, const Dictionary &inputs);
    static Dictionary compile_typed(const String &source, const Dictionary &inputs, int channels,
                                    const String &local_prefix);
};
namespace nbmaterial {
using nbdocument::dict;
String text(const Dictionary &data, const String &key, const String &fallback = "");
String code(const Dictionary &value);
int width(const Dictionary &value);
Dictionary expression(const String &source, int channels);
Array list(std::initializer_list<Variant> values);
String join(const Array &values, const String &separator);
struct Context {
    Ref<Resource> graph;
    Dictionary memo, active, uniform_keys, uniforms, features, textures, options, aliases;
    Dictionary custom_results;
    TypedArray<String> declarations, body, requirements, used_ops, vertex_body;
    TypedArray<Dictionary> bindings, errors;
    std::vector<bool> uniform_stack;
    int64_t serial = 0;
    Context(const Ref<Resource> &g = {}, const Dictionary &t = {}, const Dictionary &o = {})
        : graph(g), textures(t), options(o) {}
    Dictionary property(const StringName &name) const;
    Dictionary run();
    Dictionary root_pin(const String &name, const Dictionary &fallback, const Dictionary &scope);
    Dictionary node_map(const Array &nodes);
    Dictionary pin(const Dictionary &pin, const Dictionary &scope, const Dictionary &fallback);
    Dictionary input(const Dictionary &node, const String &name, const Dictionary &scope,
                     const Dictionary &fallback);
    Dictionary node(const Dictionary &node, const Dictionary &scope, int output = 0);
    Dictionary node_math(const String &op, const Dictionary &node, const Dictionary &scope, bool &handled);
    Dictionary node_scene(const String &op, const Dictionary &node, const Dictionary &scope, int output,
                          bool &handled);
    Dictionary function_call(const Dictionary &node, const Dictionary &scope, int output);
    Dictionary switch_node(const Dictionary &node, const Dictionary &scope);
    Dictionary texture(const Dictionary &node, const Dictionary &scope, const Dictionary &raw_object = {});
    Dictionary volume_texture(const Dictionary &node, const Dictionary &scope, const Dictionary &descriptor,
                              const String &source, const String &parameter);
    String sampler_filter_hint(const Dictionary &descriptor, bool no_mips);
    Dictionary parameter(const String &kind, const String &name, const Variant &initial, int channels);
    Array output_mask(const String &op, int output);
    Dictionary swizzle(const Dictionary &value, const Array &mask);
    Dictionary cast(const Dictionary &value, int channels);
    int common(const Dictionary &a, const Dictionary &b);
    bool same_expression(const Dictionary &a, const Dictionary &b);
    double float32(const Variant &value);
    Array literal_values(const Dictionary &value, int channels);
    Dictionary fold_binary(const String &op, const Dictionary &a, const Dictionary &b, int channels);
    Dictionary constant(Variant value);
    Array components(const Variant &value, int count);
    String number(const Variant &value);
    String type(int channels);
    void require(const String &requirement);
    void error(const String &code, const String &message);
    void feature(const String &name);
    String uv(int index);
    Dictionary view_property(const String &name, int output);
    Dictionary custom(const Dictionary &node, const Dictionary &scope, int output);
    Dictionary translated_custom(const Dictionary &node, const Dictionary &scope, int output, const Dictionary &program);
    Dictionary shader_support();
    bool is_uniform(const Dictionary &value);
    void note_uniform(const Dictionary &value);
};
} // namespace nbmaterial
} // namespace godot
