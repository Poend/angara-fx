#[compute]
#version 450
layout(local_size_x=4,local_size_y=4,local_size_z=4) in;
layout(set=0,binding=0) uniform sampler3D ambient_in;
layout(set=0,binding=1) uniform sampler3D directional_in;
layout(set=0,binding=2,rgba16f) uniform writeonly image3D ambient_out;
layout(set=0,binding=3,rgba16f) uniform writeonly image3D directional_out;
void main() {
    ivec3 cell=ivec3(gl_GlobalInvocationID);
    ivec3 size=imageSize(ambient_out);
    if(any(greaterThanEqual(cell,size))) return;
    vec3 uv=(vec3(cell)+0.5)/vec3(size);
    vec4 a=vec4(0.0),d=vec4(0.0);
    // Eight half-texel trilinear taps, NOT eight neighbouring voxel loads.
    for(int z=-1;z<=1;z+=2) for(int y=-1;y<=1;y+=2) for(int x=-1;x<=1;x+=2) {
        vec3 at=uv+0.5*vec3(x,y,z)/vec3(size);
        a+=textureLod(ambient_in,at,0.0);
        d+=textureLod(directional_in,at,0.0);
    }
    imageStore(ambient_out,cell,a*0.125);
    imageStore(directional_out,cell,d*0.125);
}
