#pragma once
#include "document_values.h"
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/texture2d.hpp>
namespace godot {
class NBNativeTextureBuilder : public RefCounted {
    GDCLASS(NBNativeTextureBuilder, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary collect_descriptors(const PackedStringArray &paths);
    static Dictionary index_sources(const PackedStringArray &paths, const Dictionary &descriptors);
    static Dictionary build(const Dictionary &indexed, const Dictionary &descriptor);
    static bool matches_descriptor(const Ref<Texture2D> &texture, const Dictionary &descriptor,
                                   const String &source);
    static String hash_bytes(const PackedByteArray &bytes, int algorithm = 2);
    static String project_path(const String &path);
    static bool positive_int(const Variant &value);
    static bool valid_sha(const String &value);
    static String enum_token(const String &value);
    static Dictionary descriptor_contract(const Dictionary &descriptor);
    static TypedArray<Dictionary> capture_evidence(const Variant &records, Dictionary result,
                                                   const String &source);
    static Ref<ImageTexture> snapshot(const Ref<Texture2D> &texture, const String &source, const String &file,
                                      const Dictionary &descriptor);
    static void apply_evidence(const Ref<Resource> &graph, const PackedStringArray &sources);
    static Dictionary read_packet(const String &path, const Ref<Resource> &graph);
    static Dictionary built_packet(const String &path, const Dictionary &manifest,
                                   const Ref<Resource> &graph);
};
namespace nbtexture {
using nbdocument::dict;
struct Format {
    Image::Format image;
    int bytes;
    int block;
};
bool format(const String &name, Format &output);
Variant json(const String &path);
void error(Dictionary result, const String &message, const String &path);
} // namespace nbtexture
} // namespace godot
