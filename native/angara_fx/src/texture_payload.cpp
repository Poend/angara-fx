#include "texture_builder.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/hashing_context.hpp>
#include <cmath>
namespace godot {
using namespace nbtexture;
Dictionary NBNativeTextureBuilder::build(const Dictionary &indexed, const Dictionary &descriptor) {
    Dictionary entry = indexed.get("entry", Dictionary());
    String source = Variant(entry.get("path", "")).stringify();
    Array diagnostics;
    Dictionary result = dict({{"texture", Variant()},
                              {"source_path", source},
                              {"diagnostics", diagnostics},
                              {"provenance", Dictionary()}});
    String path = indexed.get("payload", ""),
           package = Variant(descriptor.get("package_sha256", "")).stringify();
    if (Variant(FileAccess::get_sha256(path)) != entry.get("payload_sha256", Variant())) {
        error(result, "Native payload changed after indexing.", source);
        return result;
    }
    Variant raw = json(path);
    Dictionary payload = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
    if (raw.get_type() != Variant::DICTIONARY ||
        !nbdocument::equal(payload.get("schema_version", Variant()), 1) ||
        payload.get("producer", Variant()) != Variant("NiagaraBridgeSourceLibrary.ReadBuiltTextureMips") ||
        payload.get("status", Variant()) != Variant("exported") ||
        payload.get("object_path", Variant()) != Variant(source) ||
        payload.get("byte_encoding", Variant()) != Variant("uint8_decimal_array") ||
        bool(payload.get("source_packages_saved", true)) ||
        bool(payload.get("force_rebuild_invoked", true)) ||
        !bool(payload.get("source_package_unchanged", false)) ||
        payload.get("source_package_sha256", Variant()) != Variant(package) ||
        payload.get("source_package_sha256_before", Variant()) != Variant(package) ||
        payload.get("source_package_sha256_after", Variant()) != Variant(package)) {
        error(result, "Native payload identity/provenance is invalid.", source);
        return result;
    }
    if (payload.get("capture_evidence", Array()) != entry.get("capture_evidence", Array())) {
        error(result, "Native payload capture evidence differs from its bound manifest.", source);
        return result;
    }
    TypedArray<Dictionary> evidence =
        capture_evidence(payload.get("capture_evidence", Array()), result, source);
    String name = Variant(payload.get("pixel_format", "")).stringify();
    Format f;
    if (!format(name, f) || Variant(name) != entry.get("pixel_format", Variant()) ||
        payload.get("rhi", Variant()) != entry.get("rhi", Variant())) {
        error(result, "Native platform format is unsupported or inconsistent: " + name, source);
        return result;
    }
    for (const char *field : {"srgb", "normalize_normals", "flip_green_channel", "use_new_mip_filter"})
        if (!payload.has(field) || !descriptor.has(field) || payload[field] != descriptor[field])
            error(result, String("Native build property disagrees with the source descriptor: ") + field,
                  source);
    for (const char *field : {"compression_settings", "mip_gen_settings"})
        if (!payload.has(field) || !descriptor.has(field) ||
            enum_token(Variant(payload[field]).stringify()) !=
                enum_token(Variant(descriptor[field]).stringify()))
            error(result, String("Native build setting disagrees with the source descriptor: ") + field,
                  source);
    if ((name == "PF_BC4" || name == "PF_BC5" || name == "PF_G8") && bool(payload.get("srgb", false)))
        error(result, "Single/two-channel native source cannot use an sRGB image view.", source);
    if (!nbdocument::equal(payload.get("block_size_x", Variant()), f.block) ||
        !nbdocument::equal(payload.get("block_size_y", Variant()), f.block) ||
        !nbdocument::equal(payload.get("block_size_z", Variant()), 1) ||
        !nbdocument::equal(payload.get("block_bytes", Variant()), f.bytes))
        error(result, "Native block layout does not match the Godot Image format.", source);
    Variant mips_v = payload.get("mips", Variant()), entry_mips_v = entry.get("mips", Variant());
    Array mips = mips_v.get_type() == Variant::ARRAY ? Array(mips_v) : Array(),
          entry_mips = entry_mips_v.get_type() == Variant::ARRAY ? Array(entry_mips_v) : Array();
    if (mips_v.get_type() != Variant::ARRAY || mips.is_empty() || entry_mips_v.get_type() != Variant::ARRAY ||
        mips.size() != entry_mips.size() || mips.size() != int64_t(entry.get("mip_count", -1)))
        error(result, "Native mip inventory is missing/inconsistent.", source);
    if (!diagnostics.is_empty())
        return result;
    Variant first_v = mips[0];
    Dictionary first = first_v.get_type() == Variant::DICTIONARY ? Dictionary(first_v) : Dictionary();
    if (first_v.get_type() != Variant::DICTIONARY || !positive_int(first.get("size_x", Variant())) ||
        !positive_int(first.get("size_y", Variant()))) {
        error(result, "Native base dimensions are invalid.", source);
        return result;
    }
    // Bound floating source dimensions before conversion and allocation.
    if (double(first["size_x"]) > 32768 || double(first["size_y"]) > 32768) {
        error(result, "Native texture exceeds the supported dimension limit.", source);
        return result;
    }
    int64_t width = first["size_x"], height = first["size_y"], nw = width, nh = height, required = 1;
    while (nw > 1 || nh > 1) {
        nw = MAX(int64_t(1), nw >> 1);
        nh = MAX(int64_t(1), nh >> 1);
        ++required;
    }
    if (mips.size() != 1 && mips.size() != required) {
        error(result,
              "Godot requires a complete native mip chain or an explicit single level; missing mips are not "
              "synthesized.",
              source);
        return result;
    }
    PackedByteArray bytes;
    TypedArray<Dictionary> records;
    for (int64_t level = 0; level < mips.size(); ++level) {
        Variant mip_v = mips[level];
        Dictionary mip = mip_v.get_type() == Variant::DICTIONARY ? Dictionary(mip_v) : Dictionary();
        int64_t w = MAX(int64_t(1), width >> level), h = MAX(int64_t(1), height >> level),
                expected = ((w + f.block - 1) / f.block) * ((h + f.block - 1) / f.block) * f.bytes;
        Variant data_v = mip.get("data", Variant());
        Array data = data_v.get_type() == Variant::ARRAY ? Array(data_v) : Array();
        if (mip_v.get_type() != Variant::DICTIONARY || !nbdocument::equal(mip.get("level", Variant()), level) ||
            !nbdocument::equal(mip.get("size_x", Variant()), w) ||
            !nbdocument::equal(mip.get("size_y", Variant()), h) ||
            !nbdocument::equal(mip.get("size_z", Variant()), 1) ||
            !nbdocument::equal(mip.get("encoded_bytes", Variant()), expected) ||
            !bool(mip.get("size_matches_block_layout", false)) || data_v.get_type() != Variant::ARRAY ||
            data.size() != expected || bytes.size() + expected > 64 * 1024 * 1024) {
            error(result,
                  "Native mip dimensions/byte count/layout are invalid at level " + String::num_int64(level),
                  source);
            return result;
        }
        Dictionary metadata = mip.duplicate(false);
        metadata.erase("data");
        if (Variant(metadata) != entry_mips[level]) {
            error(result, "Native mip metadata differs from its bound manifest.", source);
            return result;
        }
        PackedByteArray encoded;
        encoded.resize(expected);
        uint8_t *destination = encoded.ptrw();
        for (int64_t i = 0; i < data.size(); ++i) {
            Variant value = data[i];
            if (!nbdocument::number(value) || !std::isfinite(double(value)) || double(value) < 0 ||
                double(value) > 255 || std::floor(double(value)) != double(value)) {
                error(result, "Native byte is not an integer in [0,255].", source);
                return result;
            }
            destination[i] = uint8_t(int64_t(value));
        }
        if (Variant(hash_bytes(encoded)) != mip.get("encoded_sha256", Variant()) ||
            hash_bytes(encoded, HashingContext::HASH_SHA1).to_lower() !=
                Variant(mip.get("encoded_sha1", "")).stringify().to_lower()) {
            error(result, "Native encoded mip checksum mismatch at level " + String::num_int64(level),
                  source);
            return result;
        }
        bytes.append_array(encoded);
        records.append(metadata);
    }
    if (!nbdocument::equal(bytes.size(), entry.get("encoded_bytes", Variant())) ||
        !nbdocument::equal(bytes.size(), payload.get("expected_encoded_bytes", Variant()))) {
        error(result, "Native total byte count is inconsistent.", source);
        return result;
    }
    int64_t first_level = 0;
    Dictionary resource = payload.get("source_resource_mips", Dictionary());
    if (int64_t(descriptor.get("lod_bias",0)) > 0 && !resource.is_empty()) {
        first_level = int64_t(resource.get("asset_lod_bias",-1));
        if (first_level < 0 || first_level >= mips.size() ||
            !nbdocument::equal(resource.get("asset_mips",Variant()),mips.size()) ||
            !nbdocument::equal(resource.get("resource_max_mips",Variant()),mips.size()-first_level)) {
            error(result,"Native resource LOD range does not match the exported platform chain.",source);
            return result;
        }
        int64_t offset = 0;
        for (int64_t i=0;i<first_level;++i) offset+=int64_t(records[i].get("encoded_bytes",0));
        bytes = bytes.slice(offset);
        width = MAX(int64_t(1),width>>first_level);
        height = MAX(int64_t(1),height>>first_level);
    }
    Ref<Image> image = Image::create_from_data(int(width), int(height), mips.size()-first_level > 1, f.image, bytes);
    if (image.is_null() || image->is_empty() || image->get_data() != bytes) {
        error(result, "Godot rejected or changed the native Image payload.", source);
        return result;
    }
    Ref<ImageTexture> texture = ImageTexture::create_from_image(image);
    Dictionary provenance = dict({{"schema_version", 1},
                                  {"source_path", source},
                                  {"package_sha256", package},
                                  {"manifest", indexed.get("manifest", "")},
                                  {"manifest_sha256", indexed.get("manifest_sha256", "")},
                                  {"payload", path},
                                  {"payload_sha256", entry["payload_sha256"]},
                                  {"native_format", name},
                                  {"godot_format", f.image},
                                  {"width", width},
                                  {"height", height},
                                  {"mip_count", mips.size()-first_level},
                                  {"encoded_bytes", bytes.size()},
                                  {"encoded_sha256", hash_bytes(bytes)},
                                  {"rhi", payload["rhi"]},
                                  {"srgb", payload["srgb"]},
                                  {"normalize_normals", payload["normalize_normals"]},
                                  {"flip_green_channel", payload["flip_green_channel"]},
                                  {"mips", records},
                                  {"native_bytes_preserved", true},
                                  {"visual_parity", "unverified"}});
    provenance["descriptor_contract"] = descriptor_contract(descriptor);
    provenance["capture_evidence"] = evidence;
    if(!resource.is_empty()) {
        provenance["source_resource_mips"]=resource;
        provenance["first_source_mip"]=first_level;
    }
    texture->set_meta("angara_fx_native_texture", provenance);
    result["texture"] = texture;
    result["provenance"] = provenance;
    return result;
}
} // namespace godot
