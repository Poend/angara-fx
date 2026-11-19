@tool
extends RefCounted
const NativeTextures=preload("native_texture_builder.gd")
const META="niagara_bridge_imported_texture"
static func build(texture: Texture2D,source: String,file: String,descriptor: Dictionary) -> ImageTexture:
	return NBNativeTextureBuilder.snapshot(texture,source,file,descriptor)
