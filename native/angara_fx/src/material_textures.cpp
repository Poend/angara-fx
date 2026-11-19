#include "material_compiler.h"
#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/image_texture3d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
namespace godot {
bool NBNativeMaterialCompiler::verified_unbiased_full_chain(const Dictionary &descriptor,
                                                            const Ref<Texture2D> &texture) {
    if (int64_t(descriptor.get("lod_bias", 0)) >= 0 || texture.is_null())
        return false;
    Dictionary build = descriptor.get("native_texture_build", Dictionary()),
               observed = build.get("verified_residency", Dictionary()),
               evidence = descriptor.get("native_texture_evidence", Dictionary()),
               native = texture->get_meta("angara_fx_native_texture", Dictionary());
    if (evidence.is_empty() ||
        native.get("package_sha256", Variant()) != evidence.get("package_sha256", Variant()) ||
        !bool(native.get("native_bytes_preserved", false)))
        return false;
    bool paired = false;
    Array records = native.get("capture_evidence", Array());
    for (int64_t i = 0; i < records.size(); ++i) {
        Dictionary record = records[i];
        if (record.get("file", Variant()) ==
                Variant(NBNativeTextureBuilder::project_path(nbmaterial::text(evidence, "manifest"))) &&
            record.get("sha256", Variant()) == evidence.get("manifest_sha256", Variant()))
            paired = true;
    }
    if (!paired || !nbdocument::equal(observed.get("asset_lod_bias", -1), 0) ||
        bool(observed.get("virtual_streaming", true)) || !bool(observed.get("resource_state_valid", false)) ||
        !bool(observed.get("fully_streamed_in", false)))
        return false;
    int64_t count = native.get("mip_count", 0);
    Ref<Image> image = texture->get_image();
    return count > 0 && image.is_valid() && image->get_mipmap_count() + 1 == count &&
           nbdocument::equal(observed.get("asset_mips", Variant()), count) &&
           nbdocument::equal(observed.get("resident_mips", Variant()), count) &&
           nbdocument::equal(observed.get("resource_max_mips", Variant()), count);
}
namespace nbmaterial {
String Context::sampler_filter_hint(const Dictionary &descriptor, bool no_mips) {
    String effective = text(descriptor, "effective_sampler_filter")
                           .trim_prefix("ETextureSamplerFilter::")
                           .trim_prefix("SF_"),
           filter = text(descriptor, "filter");
    if (effective.is_empty()) {
        if (filter.contains("TF_NEAREST"))
            effective = "Point";
        else if (filter.contains("TF_TRILINEAR"))
            effective = "Trilinear";
        else if (filter.contains("TF_BILINEAR"))
            effective = "Bilinear";
        else if (filter.contains("TF_DEFAULT")) {
            require("source_texture_group_filter_requires_comparison");
            return String("filter_linear") + (no_mips ? "" : "_mipmap");
        } else
            error("texture_filter", "Unsupported explicit texture filter: " + filter);
    }
    bool nearest = ProjectSettings::get_singleton()->get_setting(
        "rendering/textures/default_filters/use_nearest_mipmap_filter", false);
    if (effective == "AnisotropicPoint" || effective == "AnisotropicLinear") {
        int64_t level = ProjectSettings::get_singleton()->get_setting(
            "rendering/textures/default_filters/anisotropic_filtering_level", 2);
        Variant source = descriptor.get("source_max_anisotropy", Variant());
        require("source_anisotropic_filter:" + effective +
                ";Godot_max=" + String::num_int64(1LL << CLAMP(level, int64_t(0), int64_t(4))) +
                ";source_max=" + source.stringify() + ";mip_filter_backend_requires_comparison");
        if (nbdocument::number(source) && double(source) <= 1) {
            Dictionary evidence = descriptor.get("native_texture_evidence", Dictionary());
            String backend = text(evidence, "graphics_mode");
            if (effective == "AnisotropicPoint" && !backend.begins_with("D3D12"))
                error("texture_filter",
                      "Disabled native anisotropy needs a verified backend mip-filter rule.");
            if (nearest && !no_mips)
                require("disabled_source_anisotropy_requires_linear_mip_filter");
            return String("filter_linear") + (no_mips ? "" : "_mipmap");
        }
        return "filter_linear_mipmap_anisotropic";
    }
    if (effective != "Point" && effective != "Bilinear" && effective != "Trilinear") {
        error("texture_filter", "Unrecognized effective native sampler filter: " + effective);
        return "filter_linear";
    }
    if (!no_mips && nearest != (effective != "Trilinear"))
        require("source_sampler_mip_filter:" + effective +
                ";requires_Godot_use_nearest_mipmap_filter=" + Variant(effective != "Trilinear").stringify());
    return String(effective == "Point" ? "filter_nearest" : "filter_linear") + (no_mips ? "" : "_mipmap");
}
Dictionary Context::volume_texture(const Dictionary &node, const Dictionary &scope,
                                   const Dictionary &descriptor, const String &source,
                                   const String &parameter) {
    Dictionary props = node["properties"], pins = node["inputs"],
               images = descriptor.get("source_images", Dictionary());
    String sampler = text(props, "SamplerType"), mode = text(props, "MipValueMode", "TMVM_None"),
           sampler_source = text(props, "SamplerSource", "SSM_FromTextureAsset");
    String address = text(descriptor, "address_mode");
    bool shared =
        sampler_source == "SSM_Clamp_WorldGroupSettings" || sampler_source == "SSM_Wrap_WorldGroupSettings";
    bool wrap = shared ? sampler_source == "SSM_Wrap_WorldGroupSettings" : address.contains("TA_WRAP");
    if ((!shared && (sampler_source != "SSM_FromTextureAsset" || (!wrap && !address.contains("TA_CLAMP")))) ||
        (mode != "TMVM_None" && mode != "TMVM_MipLevel") ||
        (sampler != "SAMPLERTYPE_LinearColor" && sampler != "SAMPLERTYPE_Masks") ||
        descriptor.get("srgb", true) ||
        !text(descriptor, "mip_gen_settings").replace("_", "").to_lower().contains("nomipmaps") ||
        int(descriptor.get("lod_bias", 0)) != 0 || int(descriptor.get("num_cinematic_mip_levels", 0)) != 0) {
        error("volume_sampling", "Unsupported volume addressing, decode or mip chain: " + source);
        return constant(0.0);
    }
    if (shared) require("source_shared_world_sampler_filter_requires_comparison:" + source);
    require("volume_source_mip0_preserved;platform_texture_build_requires_comparison:" + source);
    String filter = shared ? "filter_linear" : sampler_filter_hint(descriptor, true);
    String key = "volume:" + source + ":" + parameter + ":" + filter + (wrap ? ":wrap" : ":clamp");
    String uniform = text(uniform_keys, key);
    if (uniform.is_empty()) {
        String base = "t" + String::num_int64(uniform_keys.size());
        uniform = text(options, "symbol_prefix") + base;
        uniform_keys[key] = uniform;
        aliases[uniform] = base;
        declarations.append("uniform sampler3D " + uniform + " : " + filter +
                            (wrap ? ", repeat_enable;" : ", repeat_disable;"));
        if (bool(options.get("embedded", false))) {
            Dictionary values = options.get("values", Dictionary());
            Ref<Texture3D> texture = values.get(base, Variant());
            if (texture.is_null())
                error("volume_uniform", "Missing embedded volume: " + source);
            uniforms[uniform] = texture;
        } else {
            Array slices = images.get("slices", Array());
            int w = images.get("width", 0), h = images.get("height", 0), d = images.get("source_slices", 0);
            if (text(descriptor, "status") != "exported" || text(images, "layout") != "source_slices" ||
                text(images, "exported_pixels") != "source_mip0" ||
                int(images.get("source_blocks", 0)) != 1 || int(images.get("source_layers", 0)) != 1 ||
                w < 1 || h < 1 || d < 1 || d != slices.size()) {
                error("volume_layout", "Incomplete volume source slices: " + source);
                return constant(0.0);
            }
            TypedArray<Ref<Image>> layers;
            Image::Format format = Image::FORMAT_MAX;
            Dictionary hashes = property("source_hashes");
            for (int z = 0; z < d; ++z) {
                Dictionary slice = slices[z];
                String path = NBNativeMaterialGraphLoader::project_path(text(slice, "file"));
                String hash = text(slice, "sha256");
                if (int(slice.get("index", -1)) != z || !bool(slice.get("saved", false)) ||
                    hash.length() != 64 || !FileAccess::file_exists(path) ||
                    FileAccess::get_sha256(path) != hash) {
                    error("volume_slice", "Volume source slice index or digest mismatch: " + path);
                    return constant(0.0);
                }
                Ref<Image> image = Image::load_from_file(path);
                if (image.is_null() || image->get_width() != w || image->get_height() != h ||
                    (z > 0 && image->get_format() != format)) {
                    error("volume_slice", "Inconsistent volume slice size or format: " + path);
                    return constant(0.0);
                }
                format = image->get_format();
                layers.append(image);
                hashes[path] = hash;
            }
            Ref<ImageTexture3D> texture;
            texture.instantiate();
            if (texture->create(format, w, h, d, false, layers) != OK) {
                error("volume_texture", "Could not create the volume texture: " + source);
                return constant(0.0);
            }
            uniforms[uniform] = texture;
        }
        bindings.append(dict({{"kind", "volume_texture"},
                              {"parameter", parameter},
                              {"uniform", uniform},
                              {"source_path", source}}));
    }
    if (bool(node.get("raw_sampler", false)))
        return dict({{"code", uniform}, {"channels", -1}, {"texture_type", "Texture3D"}});
    Dictionary coords = input(node, "Coordinates", scope, constant(0.0));
    if (!pins.has("Coordinates") || width(coords) != 3) {
        error("volume_coordinates", "Volume texture sampling requires three coordinates: " + source);
        return constant(0.0);
    }
    if (mode == "TMVM_MipLevel") {
        Dictionary mip = input(node, "MipValue", scope, constant(props.get("ConstMipValue", 0.0)));
        return expression("textureLod(" + uniform + ", " + code(coords) + ", " + code(cast(mip, 1)) + ")", 4);
    }
    return expression("texture(" + uniform + ", " + code(coords) + ")", 4);
}
Dictionary Context::texture(const Dictionary &n, const Dictionary &scope, const Dictionary &raw_object) {
    Dictionary p = n["properties"], inputs = n["inputs"], parameters = property("parameters"),
               texture_parameters = parameters["texture"];
    String op = n["op"], path = text(p, "Texture");
    bool sub_uv = op == "ParticleSubUV" || op == "TextureSampleParameterSubUV",
         texture_parameter = op == "TextureSampleParameter2D" || op == "TextureSampleParameterVolume" ||
                             op == "TextureSampleParameterSubUV";
    Dictionary texture_object = raw_object;
    if (!raw_object.is_empty())
        path = text(raw_object, "texture");
    else if (inputs.has("TextureObject") && op != "TextureSampleParameterSubUV") {
        texture_object =
            input(n, "TextureObject", scope, dict({{"code", ""}, {"channels", -1}, {"texture", ""}}));
        if (width(texture_object) != -1)
            error("texture_object_type", "TextureObject input is not a texture reference.");
        path = text(texture_object, "texture");
    }
    String name = Variant(texture_object.get("parameter", p.get("ParameterName", "Param"))).stringify();
    if (texture_parameter)
        path = Variant(texture_parameters.get(name, path)).stringify();
    path = NBNativeMaterialGraphLoader::asset_path(path);
    String key = NBNativeMaterialGraphLoader::manifest_key(property("texture_dependencies"), path);
    if (key.is_empty()) {
        error("texture_missing", "Texture dependency is unresolved: " + path);
        return constant(list({0.0, 0.0, 0.0, 0.0}));
    }
    Dictionary dependencies = property("texture_dependencies"), descriptor = dependencies[key];
    if (text(descriptor, "class") == "VolumeTexture" && (op == "TextureSample" || op == "TextureSampleParameterVolume"))
        return volume_texture(n, scope, descriptor, key, texture_parameter || !texture_object.is_empty() ? name : String());
    if (op == "TextureSampleParameterVolume") {
        error("volume_type", "A volume parameter requires a VolumeTexture: " + key);
        return constant(0.0);
    }
    for (const char *required : {"srgb", "address_x", "address_y", "filter"})
        if (!descriptor.has(required))
            error("texture_metadata", String("Texture native metadata lacks ") + required + ": " + key);
    if (!descriptor.has("mip_gen_settings"))
        require("source_mip_generation_not_recorded:" + key);
    if (!descriptor.has("lod_bias") || !descriptor.has("num_cinematic_mip_levels"))
        require("source_texture_LOD_bias_not_fully_recorded:" + key);
    String file = NBNativeMaterialGraphLoader::project_path(text(descriptor, "file"));
    bool embedded = options.get("embedded", false);
    if (descriptor.get("class", Variant()) != Variant("Texture2D") ||
        descriptor.get("status", Variant()) != Variant("exported") ||
        (!embedded && (!FileAccess::file_exists(file) ||
                       Variant(FileAccess::get_sha256(file)) != descriptor.get("sha256", "")))) {
        error("texture_provenance", "Texture2D dependency failed type/status/SHA-256 validation: " + key);
        return constant(list({0.0, 0.0, 0.0, 0.0}));
    }
    String mip_mode = text(p, "MipValueMode", "TMVM_None");
    if (mip_mode != "TMVM_None" && mip_mode != "TMVM_MipLevel" && mip_mode != "TMVM_MipBias")
        error("texture_sampling", "Unsupported explicit mip mode: " + mip_mode);
    if (mip_mode == "TMVM_MipBias" && scope.get("stage", Variant()) == Variant("vertex"))
        error("texture_sampling", "Implicit derivatives for mip bias require fragment sampling.");
    if (text(p, "SamplerSource", "SSM_FromTextureAsset") != "SSM_FromTextureAsset")
        error("texture_sampling", "Shared 2D sampler-source override requires its source sampler state.");
    String address_x = text(descriptor, "address_x"), address_y = text(descriptor, "address_y");
    if (address_x != address_y || (!address_x.contains("TA_WRAP") && !address_x.contains("TA_CLAMP")))
        error("texture_address", "Mixed/mirrored texture addressing is not implemented.");
    String sampler = text(p, "SamplerType", "SAMPLERTYPE_Color");
    PackedStringArray supported = String("SAMPLERTYPE_Color,SAMPLERTYPE_LinearColor,SAMPLERTYPE_Masks,"
                                         "SAMPLERTYPE_Alpha,SAMPLERTYPE_Normal,SAMPLERTYPE_LinearGrayscale")
                                      .split(",");
    if (!supported.has(sampler))
        error("texture_sampler_type", "Sampler decode requires a dedicated implementation: " + sampler);
    String uniform_key = "texture:" + key + ":" + sampler + ":" + (texture_parameter || !texture_object.is_empty() ? name : String());
    Ref<Texture2D> native = textures.get(key, Variant());
    String format;
    if (embedded) {
        Dictionary formats = options.get("native_texture_formats", Dictionary());
        format = text(formats, key);
    } else if (native.is_valid()) {
        Dictionary provenance = native->get_meta("angara_fx_native_texture", Dictionary());
        format = text(provenance, "native_format");
    }
    bool uses_native = !format.is_empty();
    if (!embedded && textures.has(key) &&
        !NBNativeTextureBuilder::matches_descriptor(native, descriptor, key))
        error("native_texture_binding",
              "Native texture binding does not match source/package/color metadata: " + key);
    String uniform = text(uniform_keys, uniform_key);
    if (uniform.is_empty()) {
        String base = "t" + String::num_int64(uniform_keys.size());
        uniform = text(options, "symbol_prefix") + base;
        aliases[uniform] = base;
        uniform_keys[uniform_key] = uniform;
        TypedArray<String> hints;
        hints.append(address_x.contains("TA_WRAP") ? "repeat_enable" : "repeat_disable");
        bool srgb = descriptor.get("srgb", false);
        if (srgb)
            hints.append("source_color");
        if (sampler != "SAMPLERTYPE_Color" && supported.has(sampler) && srgb)
            error("texture_color_space",
                  "Linear/alpha/normal/mask sampler has sRGB texture metadata: " + key);
        String mip_mode =
            Variant(descriptor.get("effective_mip_gen_settings", descriptor.get("mip_gen_settings", "")))
                .stringify()
                .to_lower()
                .replace("_", "");
        bool no_mips = mip_mode.contains("nomipmaps");
        hints.append(sampler_filter_hint(descriptor, no_mips));
        declarations.append("uniform sampler2D " + uniform + " : " + join(hints, ", ") + ";");
        if (embedded) {
            Dictionary values = options.get("values", Dictionary());
            uniforms[uniform] = values.get(base, Variant());
        } else if (native.is_valid())
            uniforms[uniform] = native;
        else
            uniforms[uniform] = ResourceLoader::get_singleton()->exists(file)
                                    ? Variant(ResourceLoader::get_singleton()->load(file))
                                    : Variant();
        Ref<Texture2D> texture = uniforms[uniform];
        if (texture.is_null())
            error("godot_texture",
                  "Texture dependency must be imported as a Godot Texture2D before conversion: " + file);
        else if (!embedded) {
            Ref<Image> image = texture->get_image();
            if (image.is_null() || image->is_empty() ||
                (!no_mips && !image->has_mipmaps() && (image->get_width() > 1 || image->get_height() > 1)))
                error("texture_mipmaps_missing",
                      "Source sampler requires mipmaps but the imported Godot texture has no mip chain. Run "
                      "prepare_import.gd --prepare-textures, then Godot editor import, before conversion: " +
                          file);
            else if (no_mips && image->has_mipmaps())
                error(
                    "texture_mipmaps_unexpected",
                    "Source texture uses NoMipmaps but Godot generated a mip chain. Prepare texture settings "
                    "and reimport; automatic 3D import detection must not change this source setting: " +
                        file);
            else if (native.is_null()) {
                uniforms[uniform] = NBNativeTextureBuilder::snapshot(texture, key, file, descriptor);
                require("godot_imported_mip_bytes_preserved_as_text;native_platform_equivalence_unverified:" +
                        key);
            }
        }
        Dictionary hashes = property("source_hashes");
        if (!embedded && native.is_valid()) {
            Dictionary provenance = native->get_meta("angara_fx_native_texture", Dictionary());
            hashes[text(provenance, "manifest")] = text(provenance, "manifest_sha256");
            hashes[text(provenance, "payload")] = text(provenance, "payload_sha256");
            Array evidence = provenance.get("capture_evidence", Array());
            for (int64_t i = 0; i < evidence.size(); ++i) {
                Dictionary record = evidence[i];
                hashes[record["file"]] = record["sha256"];
            }
            require("native_platform_mip_bytes_preserved:" + key +
                    ";hardware_sampling_and_visual_parity_require_comparison");
        } else if (!embedded && FileAccess::file_exists(file + ".import")) {
            hashes[file + ".import"] = FileAccess::get_sha256(file + ".import");
            Ref<ConfigFile> settings;
            settings.instantiate();
            if (settings->load(file + ".import") != OK ||
                bool(settings->get_value("params", "process/fix_alpha_border", true)))
                error("texture_pixels_modified",
                      "Godot alpha-border repair changes source RGB independently of the material graph. "
                      "Prepare reviewed texture settings and reimport with process/fix_alpha_border=false: " +
                          file);
        }
        bindings.append(
            dict({{"kind", "texture"},
                  {"parameter", texture_parameter || !texture_object.is_empty() ? name : String()},
                  {"uniform", uniform},
                  {"source_path", key}}));
        if (!embedded)
            hashes[file] = descriptor["sha256"];
    }
    Ref<Texture2D> bound = uniforms.get(uniform, Variant());
    Dictionary mip_info = bound.is_valid() ? Dictionary(bound->get_meta("angara_fx_native_texture", Dictionary())) : Dictionary();
    Dictionary resource = mip_info.get("source_resource_mips", Dictionary());
    bool resource_range = !resource.is_empty() && int64_t(descriptor.get("lod_bias",0))>0 &&
                          nbdocument::equal(mip_info.get("first_source_mip",-1),resource.get("asset_lod_bias",-2)) &&
                          nbdocument::equal(mip_info.get("mip_count",-1),resource.get("resource_max_mips",-2)) &&
                          mip_info.get("package_sha256",Variant()) == descriptor.get("package_sha256",Variant());
    if (int64_t(descriptor.get("num_cinematic_mip_levels", 0)) != 0 ||
        (int64_t(descriptor.get("lod_bias", 0)) != 0 &&
         !resource_range && !NBNativeMaterialCompiler::verified_unbiased_full_chain(descriptor, bound)))
        error("texture_lod_bias", "Nonzero source texture LOD requires a native full-chain observation with "
                                  "zero effective asset bias; cinematic mip bias is unsupported: " +
                                      key);
    if (!raw_object.is_empty())
        return dict({{"code", uniform}, {"channels", -1}, {"texture_type", "Texture2D"}});
    TypedArray<String> coordinates;
    if (sub_uv) {
        feature("particle");
        feature("particle_subuv");
        require("uv_channel_0");
        coordinates.append("nb_source_subuv(UV,0)");
        if (bool(p.get("bBlend", true)))
            coordinates.append("nb_source_subuv(UV,1)");
    } else {
        int index = int64_t(p.get("ConstCoordinate", 0));
        if (index != 0 && index != 1)
            error("texture_uv", "Texture coordinate channel exceeds UV0/UV1.");
        Dictionary v = inputs.has("Coordinates") ? input(n, "Coordinates", scope, constant(0.0))
                                                 : expression(uv(index), 2);
        coordinates.append(code(cast(v, 2)));
    }
    bool flip = false;
    if (sampler == "SAMPLERTYPE_Normal") {
        feature("normal_decode");
        if (uses_native && format != "PF_BC5")
            error("normal_platform_encoding",
                  "Native normal decode currently supports verified BC5 RG payloads: " + key);
        else if (!uses_native && (text(descriptor, "exported_pixels") != "source_mip0" ||
                                  !descriptor.has("flip_green_channel") ||
                                  !descriptor.has("normal_build_green_flip_applied")))
            error("normal_pixel_provenance",
                  "Normal map requires source-pixel and green-flip provenance: " + key);
        flip = !uses_native && bool(descriptor.get("flip_green_channel", false)) &&
               !bool(descriptor.get("normal_build_green_flip_applied", false));
    }
    Dictionary mip;
    if (mip_mode == "TMVM_MipLevel" || mip_mode == "TMVM_MipBias")
        mip = cast(input(n, "MipValue", scope, constant(p.get("ConstMipValue", -1))), 1);
    TypedArray<String> samples;
    for (int64_t i = 0; i < coordinates.size(); ++i) {
        String coordinate = coordinates[i], sample = "texture(" + uniform + ", " + coordinate + ")";
        if (mip_mode == "TMVM_MipLevel")
            sample = "textureLod(" + uniform + ", " + coordinate + ", " + code(mip) + ")";
        else if (mip_mode == "TMVM_MipBias")
            sample = "texture(" + uniform + ", " + coordinate + ", " + code(mip) + ")";
        if (sampler == "SAMPLERTYPE_Alpha" || sampler == "SAMPLERTYPE_LinearGrayscale")
            sample = "vec4(" + sample + ".r)";
        else if (sampler == "SAMPLERTYPE_Normal")
            sample = "nb_decode_normal(" + sample + ", " + String(flip ? "true" : "false") + ")";
        samples.append(sample);
    }
    String sampled = samples.size() == 1 ? String(samples[0])
                                         : "mix(" + String(samples[0]) + ", " + String(samples[1]) +
                                               ", nb_source_subuv_lerp())";
    return expression(sampled, 4);
}
} // namespace nbmaterial
} // namespace godot
