@tool
extends RefCounted
const FIELDS: Array[String] = ["effective_sampler_filter","source_max_anisotropy","effective_mip_gen_settings","mip_kernel_size","mip_sharpen","mip_downsample_with_average","mip_sharpen_without_color_shift","mip_border_color_black","normalize_normals","use_new_mip_filter","pixel_format","device_profile"]
const RESIDENCY_FIELDS: Array[String] = ["asset_lod_bias","asset_mips","resident_mips","resource_max_mips","resource_state_valid","fully_streamed_in","virtual_streaming"]
static func apply(graph: Resource,sources: PackedStringArray) -> void:NBNativeTextureBuilder.apply_evidence(graph,sources)
static func _read_packet(path: String,graph: Resource) -> Dictionary:return NBNativeTextureBuilder.read_packet(path,graph)
static func _built_packet(path: String,manifest: Dictionary,graph: Resource) -> Dictionary:return NBNativeTextureBuilder.built_packet(path,manifest,graph)
static func _enum_token(value: String) -> String:return NBNativeTextureBuilder.enum_token(value)
