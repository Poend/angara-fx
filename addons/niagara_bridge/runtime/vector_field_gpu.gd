@tool
extends NBNativeVectorFieldGPU
## Native RGBA16F volume and linear sampler, matching Niagara's GPU vector-field DI.
## Callers own the returned RIDs and must free them on the same RenderingDevice.
const SAMPLE_GLSL: String = """
vec3 nb_sample_vector_field(sampler3D field_texture, vec3 position, vec3 bounds_min, vec3 bounds_max) {
    return textureLod(field_texture, (position - bounds_min) / (bounds_max - bounds_min), 0.0).xyz;
}
"""
