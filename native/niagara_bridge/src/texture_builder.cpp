#include "texture_builder.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/hashing_context.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <cmath>
namespace godot {
namespace nbtexture {
bool format(const String &name, Format &out) {
    if (name == "PF_DXT1")
        out = {Image::FORMAT_DXT1, 8, 4};
    else if (name == "PF_DXT5")
        out = {Image::FORMAT_DXT5, 16, 4};
    else if (name == "PF_BC4")
        out = {Image::FORMAT_RGTC_R, 8, 4};
    else if (name == "PF_BC5")
        out = {Image::FORMAT_RGTC_RG, 16, 4};
    else if (name == "PF_G8")
        out = {Image::FORMAT_R8, 1, 1};
    else
        return false;
    return true;
}
Variant json(const String &path) {
    return FileAccess::file_exists(path) ? JSON::parse_string(FileAccess::get_file_as_string(path))
                                         : Variant();
}
void error(Dictionary result, const String &message, const String &path) {
    Array diagnostics = result["diagnostics"];
    diagnostics.append(dict(
        {{"severity", "error"}, {"code", "native_texture_payload"}, {"message", message}, {"path", path}}));
}
} // namespace nbtexture
using namespace nbtexture;
String NBNativeTextureBuilder::hash_bytes(const PackedByteArray &bytes, int algorithm) {
    Ref<HashingContext> context;
    context.instantiate();
    if (context->start(HashingContext::HashType(algorithm)) != OK)
        return "";
    if (!bytes.is_empty() && context->update(bytes) != OK)
        return "";
    return context->finish().hex_encode();
}
String NBNativeTextureBuilder::project_path(const String &value) {
    String path = value.replace("\\", "/");
    if (path.is_empty() || path.contains("..") ||
        (!path.begins_with("res://") && (path.is_absolute_path() || path.contains(":"))))
        return "";
    return path.begins_with("res://") ? path : "res://" + path;
}
bool NBNativeTextureBuilder::positive_int(const Variant &value) {
    return nbdocument::number(value) && std::isfinite(double(value)) && double(value) > 0 &&
           std::floor(double(value)) == double(value);
}
bool NBNativeTextureBuilder::valid_sha(const String &value) {
    return value.length() == 64 && value.is_valid_hex_number(false);
}
String NBNativeTextureBuilder::enum_token(const String &value) {
    return value.get_slice(".", value.get_slice_count(".") - 1)
        .get_slice(":", 0)
        .trim_prefix("<")
        .trim_suffix(">")
        .strip_edges()
        .to_lower()
        .replace("_", "");
}
Dictionary NBNativeTextureBuilder::descriptor_contract(const Dictionary &descriptor) {
    PackedStringArray fields =
        String("object_path,class,status,sha256,package_sha256,package_unchanged,package_hash_phase,srgb,"
               "flip_green_channel,normalize_normals,use_new_mip_filter,compression_settings,mip_gen_"
               "settings,address_x,address_y,filter,lod_group,lod_bias,num_cinematic_mip_levels,exported_"
               "pixels,normal_build_green_flip_applied")
            .split(",");
    PackedStringArray enums =
        String("compression_settings,mip_gen_settings,address_x,address_y,filter,lod_group").split(",");
    Dictionary contract;
    for (int64_t i = 0; i < fields.size(); ++i) {
        String field = fields[i];
        if (descriptor.has(field))
            contract[field] = enums.has(field) ? Variant(enum_token(Variant(descriptor[field]).stringify()))
                                               : descriptor[field];
    }
    return contract;
}
TypedArray<Dictionary> NBNativeTextureBuilder::capture_evidence(const Variant &records, Dictionary result,
                                                                const String &source) {
    TypedArray<Dictionary> accepted;
    if (records.get_type() != Variant::ARRAY) {
        error(result, "Native capture evidence inventory is malformed.", source);
        return accepted;
    }
    Array array = records;
    for (int64_t i = 0; i < array.size(); ++i) {
        if (array[i].get_type() != Variant::DICTIONARY) {
            error(result, "Native capture evidence record is malformed.", source);
            continue;
        }
        Dictionary record = array[i];
        String path = project_path(Variant(record.get("file", "")).stringify()),
               checksum = Variant(record.get("sha256", "")).stringify();
        if (path.is_empty() || !valid_sha(checksum) || !FileAccess::file_exists(path) ||
            FileAccess::get_sha256(path) != checksum) {
            error(result, "Native build capture evidence file/SHA256 validation failed.", source);
            continue;
        }
        Variant levels = record.get("matching_available_mip_hash_levels", Array());
        if (levels.get_type() != Variant::ARRAY) {
            error(result, "Native capture evidence mip inventory is malformed.", source);
            continue;
        }
        Array list = levels;
        bool valid = true;
        Dictionary seen;
        for (int64_t j = 0; j < list.size(); ++j) {
            Variant v = list[j];
            if (!nbdocument::number(v) || !std::isfinite(double(v)) || double(v) < 0 || double(v) > 31 ||
                std::floor(double(v)) != double(v) || seen.has(v))
                valid = false;
            seen[v] = true;
        }
        if (!valid) {
            error(result, "Native capture evidence mip levels are invalid.", source);
            continue;
        }
        accepted.append(dict({{"file", path},
                              {"sha256", checksum},
                              {"matching_available_mip_hash_levels", list.duplicate()}}));
    }
    return accepted;
}
Dictionary NBNativeTextureBuilder::collect_descriptors(const PackedStringArray &paths) {
    Dictionary descriptors, hashes;
    Array diagnostics;
    Dictionary result =
        dict({{"descriptors", descriptors}, {"source_hashes", hashes}, {"diagnostics", diagnostics}});
    for (int64_t i = 0; i < paths.size(); ++i) {
        String path = paths[i];
        Variant manifest = json(path);
        if (manifest.get_type() != Variant::DICTIONARY ||
            Dictionary(manifest).get("textures", Variant()).get_type() != Variant::DICTIONARY) {
            error(result, "Material manifest must contain texture descriptors.", path);
            continue;
        }
        hashes[path] = FileAccess::get_sha256(path);
        Dictionary textures = Dictionary(manifest)["textures"];
        Array keys = textures.keys();
        for (int64_t j = 0; j < keys.size(); ++j) {
            String source = keys[j];
            Variant v = textures[source];
            if (v.get_type() != Variant::DICTIONARY) {
                error(result, "Malformed source texture descriptor.", source);
                continue;
            }
            Dictionary descriptor = v;
            if (descriptor.get("object_path", Variant()) != Variant(source)) {
                error(result, "Source texture descriptor key/object identity differs.", source);
                continue;
            }
            if (descriptors.has(source)) {
                if (descriptor_contract(descriptors[source]) != descriptor_contract(descriptor))
                    error(result, "Conflicting source texture semantic descriptors.", source);
            } else
                descriptors[source] = descriptor;
        }
    }
    return result;
}
Dictionary NBNativeTextureBuilder::index_sources(const PackedStringArray &paths,
                                                 const Dictionary &descriptors) {
    Dictionary entries, hashes;
    Array diagnostics;
    Dictionary result = dict({{"entries", entries}, {"source_hashes", hashes}, {"diagnostics", diagnostics}});
    for (int64_t i = 0; i < paths.size(); ++i) {
        String path = paths[i];
        Variant packet_v = json(path);
        Dictionary packet = packet_v.get_type() == Variant::DICTIONARY ? Dictionary(packet_v) : Dictionary();
        if (packet_v.get_type() != Variant::DICTIONARY ||
            !nbdocument::equal(packet.get("schema_version", Variant()), 1) ||
            packet.get("producer", Variant()) != Variant("NiagaraBridgeSourceLibrary.ReadBuiltTextureMips") ||
            !nbdocument::equal(packet.get("producer_schema_version", Variant()), 1) ||
            bool(packet.get("source_packages_saved", true)) ||
            bool(packet.get("force_rebuild_invoked", true)) ||
            packet.get("textures", Variant()).get_type() != Variant::ARRAY) {
            error(result, "Native texture manifest must be a successful read-only schema1 packet.", path);
            continue;
        }
        hashes[path] = FileAccess::get_sha256(path);
        Array textures = packet["textures"];
        for (int64_t j = 0; j < textures.size(); ++j) {
            if (textures[j].get_type() != Variant::DICTIONARY) {
                error(result, "Malformed native texture manifest entry.", path);
                continue;
            }
            Dictionary entry = textures[j];
            String source = Variant(entry.get("path", "")).stringify();
            if (!descriptors.has(source))
                continue;
            Dictionary descriptor = descriptors[source];
            String package_hash = Variant(descriptor.get("package_sha256", "")).stringify(),
                   file = project_path(Variant(descriptor.get("file", "")).stringify());
            if (descriptor.get("object_path", Variant()) != Variant(source) ||
                descriptor.get("class", Variant()) != Variant("Texture2D") ||
                descriptor.get("status", Variant()) != Variant("exported") || file.is_empty() ||
                !FileAccess::file_exists(file) ||
                Variant(FileAccess::get_sha256(file)) != descriptor.get("sha256", "")) {
                error(result, "Source Texture2D file/status/SHA256 validation failed.", source);
                continue;
            }
            hashes[file] = descriptor["sha256"];
            Variant errors = entry.get("errors", Array());
            if (entry.get("status", Variant()) != Variant("exported") ||
                errors.get_type() != Variant::ARRAY || !Array(errors).is_empty() ||
                !valid_sha(package_hash) || !bool(descriptor.get("package_unchanged", false)) ||
                descriptor.get("package_hash_phase", Variant()) !=
                    Variant("before_and_after_readonly_texture_export") ||
                !bool(entry.get("source_package_unchanged", false)) ||
                entry.get("source_package_sha256", Variant()) != Variant(package_hash) ||
                entry.get("source_package_sha256_before", Variant()) != Variant(package_hash) ||
                entry.get("source_package_sha256_after", Variant()) != Variant(package_hash)) {
                error(result, "Native texture package/status does not match the source descriptor.", source);
                continue;
            }
            String payload = project_path(Variant(entry.get("payload", "")).stringify()),
                   checksum = Variant(entry.get("payload_sha256", "")).stringify();
            if (payload.is_empty() || !FileAccess::file_exists(payload) || !valid_sha(checksum) ||
                FileAccess::get_sha256(payload) != checksum) {
                error(result, "Native texture payload file/SHA256 validation failed.", source);
                continue;
            }
            TypedArray<Dictionary> evidence =
                capture_evidence(entry.get("capture_evidence", Array()), result, source);
            for (int64_t k = 0; k < evidence.size(); ++k) {
                Dictionary observed = evidence[k];
                hashes[observed["file"]] = observed["sha256"];
            }
            if (entries.has(source)) {
                Dictionary previous = Dictionary(entries[source])["entry"];
                if (previous["payload_sha256"] != entry["payload_sha256"])
                    error(result, "Conflicting native payloads for the same source texture.", source);
                continue;
            }
            entries[source] = dict({{"entry", entry.duplicate(true)},
                                    {"manifest", path},
                                    {"manifest_sha256", hashes[path]},
                                    {"payload", payload},
                                    {"capture_evidence", evidence}});
            hashes[payload] = checksum;
        }
    }
    return result;
}
Ref<ImageTexture> NBNativeTextureBuilder::snapshot(const Ref<Texture2D> &texture, const String &source,
                                                   const String &file, const Dictionary &descriptor) {
    if (texture.is_null())
        return Ref<ImageTexture>();
    Ref<Image> image = texture->get_image();
    if (image.is_null() || image->is_empty())
        return Ref<ImageTexture>();
    Ref<ImageTexture> frozen = ImageTexture::create_from_image(image);
    frozen->set_name(source.get_file().get_slice(".", 0));
    frozen->set_meta(
        "niagara_bridge_imported_texture",
        dict({{"schema_version", 1},
              {"source_path", source},
              {"source_file", file},
              {"source_sha256", descriptor.get("sha256", "")},
              {"import_settings_sha256", FileAccess::file_exists(file + String(".import"))
                                             ? FileAccess::get_sha256(file + String(".import"))
                                             : String()},
              {"godot_format", image->get_format()},
              {"width", image->get_width()},
              {"height", image->get_height()},
              {"mip_count", image->get_mipmap_count() + 1},
              {"encoded_sha256", hash_bytes(image->get_data())},
              {"pixel_origin", "Godot imported image and mip chain; no new filtering or conversion"},
              {"native_platform_equivalence", "unverified"}}));
    return frozen;
}
bool NBNativeTextureBuilder::matches_descriptor(const Ref<Texture2D> &texture, const Dictionary &descriptor,
                                                const String &source) {
    if (texture.is_null() || !texture->has_meta("niagara_bridge_native_texture"))
        return false;
    Variant value = texture->get_meta("niagara_bridge_native_texture");
    if (value.get_type() != Variant::DICTIONARY)
        return false;
    Dictionary p = value;
    if (!nbdocument::equal(p.get("schema_version", Variant()), 1) ||
        p.get("source_path", Variant()) != Variant(source) ||
        p.get("package_sha256", Variant()) != descriptor.get("package_sha256", Variant()) ||
        p.get("srgb", Variant()) != descriptor.get("srgb", Variant()) ||
        !bool(p.get("native_bytes_preserved", false)))
        return false;
    Dictionary observed = descriptor.get("native_texture_build", Dictionary());
    if (observed.has("pixel_format") && observed["pixel_format"] != p.get("native_format", Variant()))
        return false;
    if (p.get("descriptor_contract", Dictionary()) != Variant(descriptor_contract(descriptor)))
        return false;
    Dictionary sampling = descriptor.get("native_texture_evidence", Dictionary());
    if (sampling.get("kind", Variant()) == Variant("built_texture_export")) {
        if (p.get("manifest", Variant()) !=
                Variant(project_path(Variant(sampling.get("manifest", "")).stringify())) ||
            p.get("manifest_sha256", Variant()) != sampling.get("manifest_sha256", Variant()))
            return false;
    } else if (!sampling.is_empty()) {
        bool paired = false;
        Array evidence = p.get("capture_evidence", Array());
        for (int64_t i = 0; i < evidence.size(); ++i) {
            Dictionary e = evidence[i];
            if (e.get("file", Variant()) ==
                    Variant(project_path(Variant(sampling.get("manifest", "")).stringify())) &&
                e.get("sha256", Variant()) == sampling.get("manifest_sha256", Variant()))
                paired = true;
        }
        if (!paired)
            return false;
    }
    Ref<Image> image = texture->get_image();
    return image.is_valid() && nbdocument::equal(image->get_format(), p.get("godot_format", Variant())) &&
           nbdocument::equal(image->get_width(), p.get("width", Variant())) &&
           nbdocument::equal(image->get_height(), p.get("height", Variant())) &&
           nbdocument::equal(image->get_mipmap_count() + 1, p.get("mip_count", Variant())) &&
           Variant(hash_bytes(image->get_data())) == p.get("encoded_sha256", Variant());
}
void NBNativeTextureBuilder::_bind_methods() {
    ClassDB::bind_static_method("NBNativeTextureBuilder", D_METHOD("collect_descriptors", "paths"),
                                &NBNativeTextureBuilder::collect_descriptors);
    ClassDB::bind_static_method("NBNativeTextureBuilder", D_METHOD("index_sources", "paths", "descriptors"),
                                &NBNativeTextureBuilder::index_sources);
    ClassDB::bind_static_method("NBNativeTextureBuilder", D_METHOD("build", "indexed", "descriptor"),
                                &NBNativeTextureBuilder::build);
    ClassDB::bind_static_method("NBNativeTextureBuilder",
                                D_METHOD("matches_descriptor", "texture", "descriptor", "source"),
                                &NBNativeTextureBuilder::matches_descriptor);
    ClassDB::bind_static_method("NBNativeTextureBuilder", D_METHOD("hash_bytes", "bytes", "algorithm"),
                                &NBNativeTextureBuilder::hash_bytes, DEFVAL(2));
    ClassDB::bind_static_method("NBNativeTextureBuilder", D_METHOD("project_path", "path"),
                                &NBNativeTextureBuilder::project_path);
    ClassDB::bind_static_method("NBNativeTextureBuilder", D_METHOD("positive_int", "value"),
                                &NBNativeTextureBuilder::positive_int);
    ClassDB::bind_static_method("NBNativeTextureBuilder", D_METHOD("valid_sha", "value"),
                                &NBNativeTextureBuilder::valid_sha);
    ClassDB::bind_static_method("NBNativeTextureBuilder", D_METHOD("enum_token", "value"),
                                &NBNativeTextureBuilder::enum_token);
    ClassDB::bind_static_method("NBNativeTextureBuilder", D_METHOD("descriptor_contract", "descriptor"),
                                &NBNativeTextureBuilder::descriptor_contract);
    ClassDB::bind_static_method("NBNativeTextureBuilder",
                                D_METHOD("capture_evidence", "records", "result", "source"),
                                &NBNativeTextureBuilder::capture_evidence);
    ClassDB::bind_static_method("NBNativeTextureBuilder",
                                D_METHOD("snapshot", "texture", "source", "file", "descriptor"),
                                &NBNativeTextureBuilder::snapshot);
    ClassDB::bind_static_method("NBNativeTextureBuilder", D_METHOD("apply_evidence", "graph", "sources"),
                                &NBNativeTextureBuilder::apply_evidence);
    ClassDB::bind_static_method("NBNativeTextureBuilder", D_METHOD("read_packet", "path", "graph"),
                                &NBNativeTextureBuilder::read_packet);
    ClassDB::bind_static_method("NBNativeTextureBuilder",
                                D_METHOD("built_packet", "path", "manifest", "graph"),
                                &NBNativeTextureBuilder::built_packet);
}
} // namespace godot
