#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/script.hpp>
namespace godot {
class NBNativeSourceReader : public RefCounted {
    GDCLASS(NBNativeSourceReader, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static bool hash_matches(const String &path, const String &expected);
    static String relative_file(const String &directory, const String &relative, Array errors);
    static Dictionary vm_program(const Dictionary &script, const Dictionary &hashes, const String &directory,
                                 Array errors, Array analysis_errors);
    static Dictionary system_stages(const Dictionary &manifest, const Dictionary &hashes,
                                    const String &directory, Array errors);
    static Dictionary stages(const Ref<Resource> &emitter, const Dictionary &manifest,
                             const Dictionary &hashes, const String &directory, Array errors,
                             Array analysis_errors, const Ref<Script> &parser);
    static Dictionary system_spawn(const Dictionary &manifest, const Dictionary &hashes,
                                   const String &directory, Array errors, const Ref<Script> &parser);
};
class NBNativeExecutionSource : public RefCounted {
    GDCLASS(NBNativeExecutionSource, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary discover(const Ref<Resource> &effect);
    static Dictionary apply(const Ref<Resource> &effect, const String &path, const Ref<Script> &parser);
    static String package_hash(const Dictionary &value);
};
} // namespace godot
