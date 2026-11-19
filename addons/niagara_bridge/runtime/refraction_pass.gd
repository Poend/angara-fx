@tool
extends NBNativeRasterPass
## Authored settings and shader text; callback, submissions and GPU state are C++.
const Builder=preload("../materials/conversion/distortion_program.gd")
const Policy=preload("refraction_policy.gd")
const GeometryCache=preload("raster_geometry_cache.gd")
const COPY = """#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform sampler2D source_color;
layout(set=0,binding=1,rgba16f) uniform writeonly image2D target_color;
void main() {
 ivec2 p=ivec2(gl_GlobalInvocationID.xy),size=imageSize(target_color);
 if(any(greaterThanEqual(p,size))) return;
 imageStore(target_color,p,texelFetch(source_color,p,0));
}
"""
const APPLY = """#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform sampler2D source_color;
layout(set=0,binding=1) uniform sampler2D distortion;
layout(set=0,binding=2,rgba16f) uniform writeonly image2D target_color;
void main() {
 ivec2 p=ivec2(gl_GlobalInvocationID.xy),size=imageSize(target_color);
 if(any(greaterThanEqual(p,size))) return;
 vec2 uv=(vec2(p)+0.5)/vec2(size);
 vec4 offsets=texelFetch(distortion,p,0);
 vec2 shifted=uv+(offsets.rg-offsets.ba)*0.25;
 vec2 edge=0.5/vec2(size);
 if(any(lessThan(shifted,edge)) || any(greaterThan(shifted,vec2(1.0)-edge))) shifted=uv;
 imageStore(target_color,p,textureLod(source_color,shifted,0.0));
}
"""
@export var high_precision_offsets: bool:
	set(value):set_option("high_precision_offsets",value)
	get:return bool(get_option("high_precision_offsets"))
@export var use_imported_policy: bool:
	set(value):set_option("use_imported_policy",value)
	get:return bool(get_option("use_imported_policy"))
func _init() -> void:
	initialize_pass(false,{"builder":Builder,"raster":Builder.Raster,"policy":Policy,"geometry":GeometryCache},COPY,APPLY)
static func _uniform(type: int,binding: int,ids: Array) -> RDUniform:return NBNativeRasterPass.uniform(type,binding,ids)
static func _texture(rd: RenderingDevice,size: Vector2i,format: int,usage: int,data: Array[PackedByteArray]=[]) -> RID:return NBNativeRasterPass.texture(rd,size,format,usage,data)
static func _sampler(rd: RenderingDevice,hints: String) -> RID:return NBNativeRasterPass.sampler(rd,hints)
static func _release(rd: RenderingDevice,resources: Array) -> void:NBNativeRasterPass.release(rd,resources)
static func _release_all(rd: RenderingDevice,retired: Array,programs: Dictionary,persistent: Array) -> void:NBNativeRasterPass.release_all(rd,retired,programs,persistent)
