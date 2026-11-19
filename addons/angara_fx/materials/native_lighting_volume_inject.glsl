#[compute]
#version 450
// UE 5.8 TLV: voxel-centre injection, two-band SH with RGB band zero and
// luminance-compressed band one. All positions/directions here use UE axes/cm.
layout(local_size_x=4,local_size_y=4,local_size_z=4) in;
layout(set=0,binding=0,std140) uniform Parameters {
    vec4 minimum_voxel; // xyz world minimum, w voxel size
    ivec4 counts;       // dimension, cascade, lights, reserved
};
struct Light { vec4 position_type; vec4 direction_radius; vec4 color_inverse; vec4 falloff_cones; };
layout(set=0,binding=1,std430) readonly buffer Lights { Light lights[]; };
layout(set=0,binding=2,rgba16f) uniform writeonly image3D ambient_out;
layout(set=0,binding=3,rgba16f) uniform writeonly image3D directional_out;
const float PI=3.14159265358979323846;
const vec3 LUMINANCE=vec3(0.2126390059,0.7151686788,0.0721923153);
float saturate(float v) { return clamp(v,0.0,1.0); }
float square(float v) { return v*v; }
void main() {
    ivec3 cell=ivec3(gl_GlobalInvocationID);
    if(any(greaterThanEqual(cell,ivec3(counts.x)))) return;
    vec3 position=minimum_voxel.xyz+(vec3(cell)+0.5)*minimum_voxel.w;
    vec3 uv=(vec3(cell)+0.5)/float(counts.x);
    vec3 fade_uv=uv*(1.0+4.0/float(counts.x))-2.0/float(counts.x);
    vec3 fade=clamp((0.5-abs(fade_uv-0.5))*10.0,0.0,1.0);
    float border=fade.x*fade.y*fade.z;
    vec4 ambient=vec4(0.0), directional=vec4(0.0);
    for(int i=0;i<counts.z;i++) {
        Light light=lights[i];
        int kind=int(light.position_type.w); // 0 directional, 1 point, 2 spot, 3 Niagara simple light
        vec3 to_light=kind==0 ? -light.direction_radius.xyz : light.position_type.xyz-position;
        float distance_to_light=length(to_light);
        // A point exactly on a cell centre has no defined SH direction.
        // Keep its isotropic term finite and its directional term zero.
        vec3 L=distance_to_light>0.0 ? to_light/distance_to_light : vec3(0.0);
        float attenuation=1.0;
        if(kind!=0) {
            float radius=light.direction_radius.w;
            if(dot(to_light,to_light)>=radius*radius) continue;
            vec3 shifted=to_light+L*(0.5*minimum_voxel.w);
            float d2=dot(shifted,shifted);
            float normalized_d2=d2/(radius*radius);
            attenuation=light.color_inverse.w!=0.0
                ? square(saturate(1.0-square(normalized_d2)))/(d2+1.0)
                : pow(saturate(1.0-normalized_d2),light.falloff_cones.x);
            if(kind==2) {
                float cone=saturate((dot(L,-light.direction_radius.xyz)-light.falloff_cones.y)*light.falloff_cones.z);
                attenuation*=square(cone);
            }
            // UE fades both attenuation and shadow factor on the outer cascade.
            // SimpleLightInjectMainPS does not apply the regular local-light border fade.
            if(counts.y==1 && kind!=3) attenuation*=border*border;
        }
        vec3 incident=light.color_inverse.rgb*(attenuation/PI);
        ambient+=vec4(incident*0.282095,kind==0 ? 1.0 : 0.0);
        vec3 basis=vec3(-L.y,L.z,-L.x)*0.488603;
        directional.xyz+=basis*dot(incident,LUMINANCE);
    }
    imageStore(ambient_out,cell,ambient);
    imageStore(directional_out,cell,directional);
}
