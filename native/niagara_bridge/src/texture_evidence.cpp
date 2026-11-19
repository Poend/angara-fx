#include "texture_builder.h"
#include <godot_cpp/classes/file_access.hpp>
namespace godot {
using namespace nbtexture;
namespace {
PackedStringArray fields() {
    return String("effective_sampler_filter,source_max_anisotropy,effective_mip_gen_settings,mip_kernel_size,"
                  "mip_sharpen,mip_downsample_with_average,mip_sharpen_without_color_shift,mip_border_color_"
                  "black,normalize_normals,use_new_mip_filter,pixel_format,device_profile")
        .split(",");
}
void issue(const Ref<Resource> &graph, const String &message) {
    nbdocument::append(graph, "diagnostics",
                     dict({{"severity", "error"}, {"code", "texture_evidence"}, {"message", message}}));
}
String text(const Dictionary &d, const String &key) {
    return Variant(d.get(key, "")).stringify();
}
bool sampler(const Dictionary &d) {
    String cvar = text(d, "max_anisotropy_cvar");
    return !text(d, "effective_sampler_filter").is_empty() &&
           !text(d, "effective_mip_gen_settings").is_empty() && cvar.is_valid_float() &&
           cvar.to_float() == double(d.get("source_max_anisotropy", 0));
}
} // namespace
void NBNativeTextureBuilder::apply_evidence(const Ref<Resource> &graph, const PackedStringArray &sources) {
    if (graph.is_null())
        return;
    Dictionary accepted, dependencies = graph->get("texture_dependencies");
    for (int64_t i = 0; i < sources.size(); ++i) {
        String path = sources[i];
        Dictionary packet = read_packet(path, graph);
        if (packet.is_empty())
            continue;
        Dictionary textures = packet["textures"];
        Array keys = textures.keys();
        for (int64_t j = 0; j < keys.size(); ++j) {
            String source = keys[j];
            if (!dependencies.has(source))
                continue;
            Dictionary native = textures[source], descriptor = dependencies[source];
            if (!bool(descriptor.get("package_unchanged", false)) ||
                descriptor.get("package_hash_phase", Variant()) !=
                    Variant("before_and_after_readonly_texture_export")) {
                issue(graph, "Material export has no unchanged texture package observation: " + source);
                continue;
            }
            if (text(descriptor, "package_sha256").length() != 64 ||
                descriptor["package_sha256"] != native.get("package_sha256", Variant())) {
                issue(graph, "Texture package hash does not match the native sampling evidence: " + source);
                continue;
            }
            bool incompatible = false;
            for (const char *field :
                 {"srgb", "flip_green_channel", "normalize_normals", "use_new_mip_filter", "mip_gen_settings",
                  "filter", "compression_settings", "address_x", "address_y"}) {
                if (!descriptor.has(field) || !native.has(field))
                    continue;
                Variant a = descriptor[field], b = native[field];
                bool same = a == b;
                if (a.get_type() == Variant::STRING && b.get_type() == Variant::STRING)
                    same = enum_token(a) == enum_token(b);
                if (!same) {
                    issue(graph, String("Material texture metadata conflicts with native observation (") +
                                     field + "): " + source);
                    incompatible = true;
                }
            }
            if (incompatible)
                continue;
            Dictionary semantics;
            PackedStringArray list = fields();
            for (int64_t k = 0; k < list.size(); ++k)
                if (native.has(list[k]))
                    semantics[list[k]] = native[list[k]];
            if (native.has("verified_residency"))
                semantics["verified_residency"] = Dictionary(native["verified_residency"]).duplicate(true);
            if (accepted.has(source) && accepted[source] != Variant(semantics)) {
                issue(graph, "Conflicting native texture sampling observations: " + source);
                continue;
            }
            accepted[source] = semantics;
            for (const char *field :
                 {"effective_sampler_filter", "source_max_anisotropy", "effective_mip_gen_settings"})
                descriptor[field] = native[field];
            descriptor["native_texture_build"] = semantics;
            Dictionary evidence = dict({{"manifest", path},
                                        {"manifest_sha256", packet["manifest_sha256"]},
                                        {"run_sha256", packet["run_sha256"]},
                                        {"native_manifest_sha256", packet["native_manifest_sha256"]},
                                        {"package_sha256", native["package_sha256"]},
                                        {"export_package_file", descriptor.get("package_file", "")},
                                        {"observed_package_file", native["package_file"]},
                                        {"graphics_mode", packet["graphics_mode"]}});
            if (packet.get("kind", Variant()) == Variant("built_texture_export"))
                evidence["kind"] = "built_texture_export";
            descriptor["native_texture_evidence"] = evidence;
        }
    }
}
Dictionary NBNativeTextureBuilder::read_packet(const String &path, const Ref<Resource> &graph) {
    Variant raw = json(path);
    Dictionary manifest = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
    if (raw.get_type() == Variant::DICTIONARY &&
        manifest.get("producer", Variant()) == Variant("NiagaraBridgeSourceLibrary.ReadBuiltTextureMips"))
        return built_packet(path, manifest, graph);
    String run_path = path.get_base_dir().path_join("run.json");
    Variant run_v = json(run_path);
    Dictionary run = run_v.get_type() == Variant::DICTIONARY ? Dictionary(run_v) : Dictionary();
    if (raw.get_type() != Variant::DICTIONARY || run_v.get_type() != Variant::DICTIONARY) {
        issue(graph, "Native texture evidence requires capture manifest and run.json: " + path);
        return Dictionary();
    }
    if (int64_t(manifest.get("schema_version", 0)) != 1 || int64_t(manifest.get("failures", -1)) != 0 ||
        bool(manifest.get("original_packages_saved", true)) || int64_t(run.get("exit_code", -1)) != 0 ||
        !bool(run.get("source_file_unchanged", false))) {
        issue(graph, "Native texture observation was not a successful read-only capture: " + path);
        return Dictionary();
    }
    String checksum = FileAccess::get_sha256(path);
    if (run.get("manifest_sha256", "") != Variant(checksum) ||
        text(run, "source_sha256_before").length() != 64 ||
        run.get("source_sha256_before", Variant()) != run.get("source_sha256_after", Variant())) {
        issue(graph, "Capture manifest/source hashes do not match run.json: " + path);
        return Dictionary();
    }
    Variant binding_v = manifest.get("texture_package_binding", Variant());
    Dictionary binding = binding_v.get_type() == Variant::DICTIONARY ? Dictionary(binding_v) : Dictionary();
    if (binding_v.get_type() != Variant::DICTIONARY ||
        !nbdocument::equal(binding.get("schema_version", Variant()), 1) ||
        run.get("texture_package_binding", Variant()) != Variant(binding) ||
        binding.get("package_hash_phase", Variant()) != Variant("after_native_readonly_capture")) {
        issue(graph, "Capture has no verified texture package binding: " + path);
        return Dictionary();
    }
    String native_name = text(binding, "native_manifest");
    if (native_name.is_empty() || native_name != native_name.get_file()) {
        issue(graph, "Native manifest must be a local filename: " + path);
        return Dictionary();
    }
    String native_path = path.get_base_dir().path_join(native_name);
    Variant original_v = json(native_path);
    Dictionary original_manifest =
        original_v.get_type() == Variant::DICTIONARY ? Dictionary(original_v) : Dictionary();
    if (original_v.get_type() != Variant::DICTIONARY ||
        Variant(FileAccess::get_sha256(native_path)) != binding.get("native_manifest_sha256", "")) {
        issue(graph, "Original native manifest hash does not match its binding: " + path);
        return Dictionary();
    }
    Variant blocks_v = manifest.get("texture_residency", Variant()),
            original_blocks_v = original_manifest.get("texture_residency", Variant());
    Array blocks = blocks_v.get_type() == Variant::ARRAY ? Array(blocks_v) : Array(),
          original_blocks =
              original_blocks_v.get_type() == Variant::ARRAY ? Array(original_blocks_v) : Array();
    if (blocks_v.get_type() != Variant::ARRAY || original_blocks_v.get_type() != Variant::ARRAY ||
        blocks.size() != original_blocks.size()) {
        issue(graph, "Texture residency observations differ from the native manifest: " + path);
        return Dictionary();
    }
    Dictionary textures;
    for (int64_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].get_type() != Variant::DICTIONARY ||
            original_blocks[i].get_type() != Variant::DICTIONARY) {
            issue(graph, "Malformed texture residency observation: " + path);
            return Dictionary();
        }
        Dictionary block = blocks[i], original = original_blocks[i];
        if (block.get("after", Array()) != original.get("after", Array())) {
            issue(graph, "Texture residency changed after native observation: " + path);
            return Dictionary();
        }
        Variant entries_v = block.get("platform_textures", Array()),
                native_entries_v = original.get("platform_textures", Array());
        Array entries = entries_v.get_type() == Variant::ARRAY ? Array(entries_v) : Array(),
              native_entries =
                  native_entries_v.get_type() == Variant::ARRAY ? Array(native_entries_v) : Array();
        if (entries_v.get_type() != Variant::ARRAY || native_entries_v.get_type() != Variant::ARRAY ||
            entries.size() != native_entries.size()) {
            issue(graph, "Platform texture observations differ from the native manifest: " + path);
            return Dictionary();
        }
        for (int64_t j = 0; j < entries.size(); ++j) {
            if (entries[j].get_type() != Variant::DICTIONARY ||
                native_entries[j].get_type() != Variant::DICTIONARY) {
                issue(graph, "Malformed platform texture record: " + path);
                return Dictionary();
            }
            Dictionary item = entries[j], native = native_entries[j], compare = item.duplicate(true),
                       compare_native = native.duplicate(true);
            for (const char *field :
                 {"package_file", "package_sha256", "package_hash_phase", "source_max_anisotropy"})
                compare.erase(field);
            compare_native.erase("source_max_anisotropy");
            if (compare != compare_native) {
                issue(graph, "Enriched texture record altered native observations: " + path);
                return Dictionary();
            }
            String source = text(item, "path");
            if (source.is_empty() || text(item, "package_sha256").length() != 64 ||
                text(item, "package_file").is_empty() ||
                item.get("package_hash_phase", Variant()) != Variant("after_native_readonly_capture")) {
                issue(graph, "Texture lacks exact source/package identity: " + path);
                return Dictionary();
            }
            if (!nbdocument::number(item.get("source_max_anisotropy", Variant()))) {
                issue(graph, "Texture source anisotropy is not numeric: " + source);
                return Dictionary();
            }
            if (!sampler(item)) {
                issue(graph, "Effective native sampler settings are incomplete or inconsistent: " + source);
                return Dictionary();
            }
            item = item.duplicate(true);
            Array after = original.get("after", Array());
            for (int64_t k = 0; k < after.size(); ++k) {
                Dictionary observation = after[k];
                if (observation.get("path", Variant()) != Variant(source))
                    continue;
                Dictionary residency;
                for (const char *field :
                     {"asset_lod_bias", "asset_mips", "resident_mips", "resource_max_mips",
                      "resource_state_valid", "fully_streamed_in", "virtual_streaming"})
                    if (observation.has(field))
                        residency[field] = observation[field];
                item["verified_residency"] = residency;
            }
            if (textures.has(source)) {
                Dictionary previous = textures[source];
                PackedStringArray compare_fields = fields();
                compare_fields.append("package_file");
                compare_fields.append("package_sha256");
                compare_fields.append("verified_residency");
                for (int64_t k = 0; k < compare_fields.size(); ++k)
                    if (previous.get(compare_fields[k], Variant()) !=
                        item.get(compare_fields[k], Variant())) {
                        issue(graph, "Native sampler changed during capture: " + source);
                        return Dictionary();
                    }
            }
            textures[source] = item;
        }
    }
    if (textures.is_empty()) {
        issue(graph, "Capture contains no native platform texture observations: " + path);
        return Dictionary();
    }
    Dictionary hashes = graph->get("source_hashes");
    for (const String &file : {path, run_path, native_path})
        hashes[file] = FileAccess::get_sha256(file);
    return dict({{"textures", textures},
                 {"manifest_sha256", checksum},
                 {"run_sha256", FileAccess::get_sha256(run_path)},
                 {"native_manifest_sha256", binding["native_manifest_sha256"]},
                 {"graphics_mode", text(run, "graphics_mode")}});
}
Dictionary NBNativeTextureBuilder::built_packet(const String &path, const Dictionary &manifest,
                                                const Ref<Resource> &graph) {
    if (!nbdocument::equal(manifest.get("schema_version", Variant()), 1) ||
        manifest.get("source_packages_saved", Variant()) != Variant(false) ||
        manifest.get("force_rebuild_invoked", Variant()) != Variant(false) ||
        manifest.get("textures", Variant()).get_type() != Variant::ARRAY) {
        issue(graph, "Invalid original built-texture export: " + path);
        return Dictionary();
    }
    Dictionary textures;
    String backend;
    Array entries = manifest["textures"];
    for (int64_t i = 0; i < entries.size(); ++i) {
        if (entries[i].get_type() != Variant::DICTIONARY) {
            issue(graph, "Invalid original built-texture export: " + path);
            return Dictionary();
        }
        Dictionary entry = entries[i];
        String file = text(entry, "payload");
        if (file.is_empty() || file.is_absolute_path() || file.contains(":") || file.contains("..")) {
            issue(graph, "Invalid native texture payload path");
            return Dictionary();
        }
        file = "res://" + file;
        Variant raw = json(file);
        Dictionary native = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
        String checksum = text(entry, "source_package_sha256");
        if (raw.get_type() != Variant::DICTIONARY ||
            Variant(FileAccess::get_sha256(file)) != entry.get("payload_sha256", Variant()) ||
            entry.get("status", Variant()) != Variant("exported") ||
            entry.get("errors", Variant()) != Variant(Array()) || checksum.length() != 64) {
            issue(graph, "Native texture payload identity/hash mismatch: " + file);
            return Dictionary();
        }
        if (native.get("producer", Variant()) != manifest.get("producer", Variant()) ||
            native.get("status", Variant()) != Variant("exported") ||
            native.get("object_path", Variant()) != entry.get("path", Variant()) ||
            native.get("source_packages_saved", Variant()) != Variant(false) ||
            native.get("force_rebuild_invoked", Variant()) != Variant(false)) {
            issue(graph, "Native texture payload producer/source mismatch: " + file);
            return Dictionary();
        }
        for (const Dictionary &row : {entry, native})
            if (row.get("source_package_unchanged", Variant()) != Variant(true) ||
                row.get("source_package_sha256_before", Variant()) != Variant(checksum) ||
                row.get("source_package_sha256_after", Variant()) != Variant(checksum)) {
                issue(graph, "Texture package changed during export: " + file);
                return Dictionary();
            }
        if (native.get("source_package_sha256", Variant()) != Variant(checksum) ||
            !nbdocument::number(native.get("source_max_anisotropy", Variant()))) {
            issue(graph, "Missing native sampler/package evidence: " + file);
            return Dictionary();
        }
        if (!sampler(native)) {
            issue(graph, "Incomplete effective native sampler: " + file);
            return Dictionary();
        }
        String source = text(entry, "path"), rhi = text(native, "rhi");
        if (textures.has(source) || (!backend.is_empty() && backend != rhi)) {
            issue(graph, "Conflicting built-texture evidence");
            return Dictionary();
        }
        backend = rhi;
        if (backend.is_empty()) {
            issue(graph, "Native graphics backend is missing");
            return Dictionary();
        }
        native["package_sha256"] = checksum;
        native["package_file"] = native.get("source_package_file", "");
        textures[source] = native;
        Dictionary hashes = graph->get("source_hashes");
        hashes[file] = entry["payload_sha256"];
    }
    Dictionary hashes = graph->get("source_hashes");
    hashes[path] = FileAccess::get_sha256(path);
    return dict({{"textures", textures},
                 {"kind", "built_texture_export"},
                 {"manifest_sha256", FileAccess::get_sha256(path)},
                 {"run_sha256", ""},
                 {"native_manifest_sha256", ""},
                 {"graphics_mode", backend}});
}
} // namespace godot
