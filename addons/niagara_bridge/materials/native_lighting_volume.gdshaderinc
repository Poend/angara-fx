// Shared material-side TLV reconstruction. Textures are transient GPU data;
// the source light records and settings remain ordinary authored resources.
uniform sampler3D nb_tlv_ambient_inner : filter_linear, repeat_disable;
uniform sampler3D nb_tlv_ambient_outer : filter_linear, repeat_disable;
uniform sampler3D nb_tlv_directional_inner : filter_linear, repeat_disable;
uniform sampler3D nb_tlv_directional_outer : filter_linear, repeat_disable;
uniform vec4 nb_tlv_min_inner;
uniform vec4 nb_tlv_min_outer;
uniform vec4 nb_tlv_inverse_inner;
uniform vec4 nb_tlv_inverse_outer;
vec3 nb_tlv_ue_direction(vec3 godot) { return vec3(-godot.z,godot.x,godot.y); }
void nb_tlv_sample(vec3 world_m, vec3 offset_ue, out vec4 ambient, out vec3 directional) {
    vec3 position=nb_tlv_ue_direction(world_m)*100.0;
    vec3 inner=(position+offset_ue*nb_tlv_inverse_inner.w-nb_tlv_min_inner.xyz)*nb_tlv_inverse_inner.xyz;
    vec3 outer=(position+offset_ue*nb_tlv_inverse_outer.w-nb_tlv_min_outer.xyz)*nb_tlv_inverse_outer.xyz;
    vec3 fade=clamp((0.5-abs(inner-0.5))*6.0,0.0,1.0);
    float weight=fade.x*fade.y*fade.z;
    ambient=mix(textureLod(nb_tlv_ambient_outer,outer,0.0),textureLod(nb_tlv_ambient_inner,inner,0.0),weight);
    directional=mix(textureLod(nb_tlv_directional_outer,outer,0.0).rgb,textureLod(nb_tlv_directional_inner,inner,0.0).rgb,weight);
}
vec3 nb_tlv_diffuse(vec4 ambient, vec3 directional, vec3 normal_world, float intensity, bool use_direction) {
    vec3 result=ambient.rgb*(0.282095*3.14159265358979323846);
    if(use_direction) {
        vec3 normal_ue=nb_tlv_ue_direction(normal_world)*intensity;
        vec3 transfer=vec3(-normal_ue.y,normal_ue.z,-normal_ue.x)*(0.488603*2.0*3.14159265358979323846/3.0);
        vec3 normalized_color=ambient.rgb/(dot(ambient.rgb,vec3(0.2126390059,0.7151686788,0.0721923153))+0.00001);
        result+=normalized_color*dot(directional,transfer);
    }
    return max(result,vec3(0.0));
}
