@tool
extends RefCounted
## Original platform bytes and mips; authored resources remain ordinary text.
const PRODUCER = "NiagaraBridgeSourceLibrary.ReadBuiltTextureMips"
const FORMATS = {"PF_DXT1":[Image.FORMAT_DXT1,8,4],"PF_DXT5":[Image.FORMAT_DXT5,16,4],"PF_BC4":[Image.FORMAT_RGTC_R,8,4],"PF_BC5":[Image.FORMAT_RGTC_RG,16,4],"PF_G8":[Image.FORMAT_R8,1,1]}
const MAX_ENCODED_BYTES = 64 * 1024 * 1024
const DESCRIPTOR_FIELDS: Array[String] = ["object_path","class","status","sha256","package_sha256","package_unchanged","package_hash_phase","srgb","flip_green_channel","normalize_normals","use_new_mip_filter","compression_settings","mip_gen_settings","address_x","address_y","filter","lod_group","lod_bias","num_cinematic_mip_levels","exported_pixels","normal_build_green_flip_applied"]
const ENUM_FIELDS: Array[String] = ["compression_settings","mip_gen_settings","address_x","address_y","filter","lod_group"]
static func collect_descriptors(paths: PackedStringArray) -> Dictionary:return NBNativeTextureBuilder.collect_descriptors(paths)
static func index_sources(paths: PackedStringArray,descriptors: Dictionary) -> Dictionary:return NBNativeTextureBuilder.index_sources(paths,descriptors)
static func build(indexed: Dictionary,descriptor: Dictionary) -> Dictionary:return NBNativeTextureBuilder.build(indexed,descriptor)
static func matches_descriptor(texture: Texture2D,descriptor: Dictionary,source: String) -> bool:return NBNativeTextureBuilder.matches_descriptor(texture,descriptor,source)
static func hash_bytes(bytes: PackedByteArray,algorithm: int=HashingContext.HASH_SHA256) -> String:return NBNativeTextureBuilder.hash_bytes(bytes,algorithm)
static func project_path(path: String) -> String:return NBNativeTextureBuilder.project_path(path)
static func _positive_int(value: Variant) -> bool:return NBNativeTextureBuilder.positive_int(value)
static func _valid_sha(value: String) -> bool:return NBNativeTextureBuilder.valid_sha(value)
static func _enum_token(value: String) -> String:return NBNativeTextureBuilder.enum_token(value)
static func _descriptor_contract(descriptor: Dictionary) -> Dictionary:return NBNativeTextureBuilder.descriptor_contract(descriptor)
static func _capture_evidence(records: Variant,result: Dictionary,source: String) -> Array[Dictionary]:return NBNativeTextureBuilder.capture_evidence(records,result,source)
