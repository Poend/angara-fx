@tool
extends RefCounted
## Transient signed/HDR vertex colors are packed by the native renderer.
static func update(texture: ImageTexture, colors: PackedColorArray) -> ImageTexture:
	return NBNativeRendererGeometry.vertex_colors(texture,colors)
