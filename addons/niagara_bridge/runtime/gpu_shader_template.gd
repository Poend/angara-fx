@tool
extends RefCounted
## Text-only GLSL template. SPIR-V and buffers are transient RenderingDevice data.
const CACHE_SLOTS: int = 16
const STRIDE_VEC4: int = 48
const SOURCE: String = """#version 450
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_ballot : require
layout(local_size_x=64, local_size_y=1, local_size_z=1) in;
layout(set=0,binding=0,std430) restrict buffer Particles { vec4 data[]; } particles;
layout(set=0,binding=1,std430) readonly buffer Parameters { vec4 data[]; } params;
layout(set=0,binding=2,std430) buffer OutputCount { uint count; } output_count;
struct Particle {
 vec3 position; float age; float normalized_age; vec3 velocity; float lifetime;
 vec3 alignment; float rotation; vec2 size; vec2 initial_size;
 vec4 color; vec4 initial_color; vec4 dynamic_value; vec4 dynamic_value1; vec4 dynamic_value2; vec4 dynamic_value3;
 float mass; float uv_flip; float uv_flip_y; float subimage; float material_random; float camera_offset;
 vec3 force; float drag; vec3 previous_velocity; float alive;
 vec3 presolve_position; float birth_time; vec3 presolve_velocity; float id;
 vec3 presolve_force; float rotational_drag; vec3 advection; bool ignore_mass;
 vec2 size_factor; uint seed; uint cache_mask; int spawn_group; mat3 birth_linear; mat3 birth_rotation;
 vec3 mesh_scale; vec3 initial_mesh_scale; vec3 mesh_scale_factor; float mesh_index;
 vec4 mesh_orientation; vec4 initial_mesh_orientation; vec4 initial_rotation;
 vec3 initial_position; vec3 birth_origin; bool first_frame;
 bool spawned_this_dispatch;
 vec2 spawn_execution; vec2 update_execution; vec2 execution;
 vec4 cache[16];
 /*NB_ATTRIBUTE_FIELDS*/
};
float nb_random(inout Particle p) {
 p.seed=p.seed*747796405u+2891336453u;
 uint word=((p.seed>>((p.seed>>28u)+4u))^p.seed)*277803737u;
 return float((word>>22u)^word)*(1.0/4294967296.0);
}
float nb_range(inout Particle p,float a,float b) { return mix(a,b,nb_random(p)); }
vec4 nb_hue_shift(vec4 color,float hue,float saturation,float value) {
 float h=hue*360.0; float s=max(saturation,0.0); float v=max(value,0.0);
 float c=v*s*cos(h*3.1415927/180.0); float n=v*s*sin(h*3.1415927/180.0);
 return vec4((.299*v+.701*c+.168*n)*color.r+(.587*v-.587*c+.330*n)*color.g+(.114*v-.114*c-.497*n)*color.b,
 (.299*v-.299*c-.328*n)*color.r+(.587*v+.413*c+.035*n)*color.g+(.114*v-.114*c+.292*n)*color.b,
 (.299*v-.300*c+1.25*n)*color.r+(.587*v-.588*c-1.05*n)*color.g+(.114*v+.886*c-.203*n)*color.b,color.a);
}
vec3 nb_ue_rand3(uint id,uint call_id,uint emitter_seed,uint counter) {
 uvec3 v=uvec3(id,call_id,counter|(emitter_seed<<16u));
 v=v*1664525u+1013904223u;
 v.x+=v.y*v.z; v.y+=v.z*v.x; v.z+=v.x*v.y;
 v.x+=v.y*v.z; v.y+=v.z*v.x; v.z+=v.x*v.y;
 return vec3(v>>8u)*(1.0/16777216.0);
}
vec4 nb_ue_rand4(uint id,uint call_id,uint emitter_seed,uint counter) {
 uvec4 v=uvec4(counter,id,call_id,emitter_seed);
 v=v*1664525u+1013904223u;
 v.x+=v.y*v.w; v.y+=v.z*v.x; v.z+=v.x*v.y; v.w+=v.y*v.z;
 v.x+=v.y*v.w; v.y+=v.z*v.x; v.z+=v.x*v.y; v.w+=v.y*v.z;
 return vec4(v>>8u)*(1.0/16777216.0);
}
// Native Niagara GPU angle approximation. GLSL acos is lowered differently
// by Godot's D3D12 backend. See docs/module_semantics.md for source/evidence.
// Keep this separate from CPU VectorVM acos and independent of asset names.
float nb_ue_gpu_acos(float x) {
 float a=abs(clamp(x,-1.0,1.0));
 float angle=(((-0.0187293*a+0.0742610)*a-0.2121144)*a+1.5707288)*sqrt(1.0-a);
 return x<0.0?3.141592653589793-angle:angle;
}
vec3 nb_ue_cone(float cosine_sample,float azimuth,vec3 axis,float speed) {
 float theta=nb_ue_gpu_acos(cosine_sample);
 vec3 direction=vec3(cos(azimuth)*sin(theta),sin(azimuth)*sin(theta),cos(theta));
 axis=length(axis)>0.00001?normalize(axis):vec3(1,0,0);
 vec3 from=vec3(0,0,1); float norm_ab=sqrt(dot(from,from)*dot(axis,axis));
 float w=norm_ab+dot(from,axis);
 vec4 q=w>=0.000001*norm_ab?vec4(cross(from,axis),w):vec4(0,-1,0,0);
 q=norm_ab<0.000001?vec4(0,0,0,1):normalize(q);
 vec3 rotated=q.xyz*(dot(q.xyz,direction)*2.0)+(q.w*q.w-dot(q.xyz,q.xyz))*direction+cross(q.xyz,direction)*(q.w*2.0);
 return rotated*speed;
}
vec3 nb_unit(vec3 v) { float s=length(v); return s>0.0 ? v/s : vec3(0.0); }
float nb_age(Particle p) { return p.normalized_age; }
vec3 nb_owner_scale(Particle p,bool spawning) {
 mat3 linear=spawning ? p.birth_linear : mat3(params.data[1].xyz,params.data[2].xyz,params.data[3].xyz);
 return vec3(length(linear[0]),length(linear[1]),length(linear[2]))*sign(determinant(linear));
}
vec3 nb_transform(vec3 v,int space,bool scaled,Particle p,bool spawning) {
 bool local_space=params.data[7].x>0.5;
 mat3 linear=spawning ? p.birth_linear : mat3(params.data[1].xyz,params.data[2].xyz,params.data[3].xyz);
 mat3 rotation=spawning ? p.birth_rotation : mat3(params.data[4].xyz,params.data[5].xyz,params.data[6].xyz);
 mat3 transform=scaled ? linear : rotation;
 if(space==1 && !local_space) return transform*v;
 if(space==2 && local_space) return inverse(transform)*v;
 return v;
}
vec3 nb_origin(Particle p,bool spawning) { return spawning?p.birth_origin:vec3(params.data[4].w,params.data[5].w,params.data[6].w); }
vec3 nb_transform_between(vec3 v,int source_space,int destination_space,bool scaled,bool position,Particle p,bool spawning) {
 int simulation=params.data[7].x>0.5?1:2;
 if(source_space==0) source_space=simulation;
 if(destination_space==0) destination_space=simulation;
 if(source_space==destination_space) return v;
 mat3 linear=spawning?p.birth_linear:mat3(params.data[1].xyz,params.data[2].xyz,params.data[3].xyz);
 mat3 rotation=spawning?p.birth_rotation:mat3(params.data[4].xyz,params.data[5].xyz,params.data[6].xyz);
 mat3 transform=scaled?linear:rotation;
 vec3 origin=position?nb_origin(p,spawning):vec3(0);
 return source_space==1?transform*v+origin:inverse(transform)*(v-origin);
}
vec4 nb_quat_mul(vec4 a,vec4 b) { return vec4(a.w*b.xyz+b.w*a.xyz+cross(a.xyz,b.xyz),a.w*b.w-dot(a.xyz,b.xyz)); }
vec3 nb_safe_direction(vec3 v) { return dot(v,v)<0.0000000001?vec3(1,0,0):normalize(v); }
vec4 nb_rotation_vector(vec3 turns,float dt,vec4 existing) {
 vec3 v=turns*dt*6.283185307179586;
 if(dot(v,v)<0.0000000001) return existing;
 float angle=length(v); return nb_quat_mul(vec4(v/angle*sin(angle*0.5),cos(angle*0.5)),existing);
}
vec4 nb_random_quaternion(vec3 u) {
 float a=sqrt(max(1.0-u.x,0.0)),b=sqrt(max(u.x,0.0));
 return normalize(vec4(a*sin(6.283185307179586*u.y),a*cos(6.283185307179586*u.y),b*sin(6.283185307179586*u.z),b*cos(6.283185307179586*u.z)));
}
vec4 nb_find_between_raw(vec3 a,vec3 b) {
 float norm_ab=sqrt(dot(a,a)*dot(b,b));
 if(norm_ab<0.000001) return vec4(0,0,0,1);
 float w=norm_ab+dot(a,b); vec3 xyz=cross(a,b);
 if(w<0.000001*norm_ab) { xyz=abs(a.x)>abs(a.y)?vec3(-a.z,0,a.x):vec3(0,-a.z,a.y); w=0.0; }
 return normalize(vec4(xyz,w));
}
vec4 nb_find_between(vec3 a,vec3 b) { return nb_find_between_raw(nb_safe_direction(a),nb_safe_direction(b)); }
vec4 nb_matrix_quaternion(vec3 x,vec3 y,vec3 z) {
 // Exact source GPU fast-path branch ordering, including diagonal ties.
 float trace=x.x+y.y+z.z;
 if(trace>0.0) { float invs=inversesqrt(trace+1.0),s=0.5*invs; return vec4((y.z-z.y)*s,(z.x-x.z)*s,(x.y-y.x)*s,0.5/invs); }
 if(x.x>y.y && x.x>z.z) { float invs=inversesqrt(x.x-y.y-z.z+1.0),s=0.5*invs; return vec4(0.5/invs,(x.y+y.x)*s,(x.z+z.x)*s,(y.z-z.y)*s); }
 if(y.y>z.z) { float invs=inversesqrt(y.y-z.z-x.x+1.0),s=0.5*invs; return vec4((y.x+x.y)*s,0.5/invs,(y.z+z.y)*s,(z.x-x.z)*s); }
 float invs=inversesqrt(z.z-x.x-y.y+1.0),s=0.5*invs; return vec4((z.x+x.z)*s,(z.y+y.z)*s,0.5/invs,(x.y-y.x)*s);
}
vec4 nb_stabilize(vec3 facing,vec3 side,vec3 up,int mode,vec3 reference) {
 if(mode==0) return nb_find_between(reference,facing);
 if(mode==3) return nb_matrix_quaternion(facing,side,up);
 vec3 x=dot(facing,facing)<0.0001?vec3(1,0,0):normalize(facing);
 if(mode==2) {
  vec3 z=dot(up,up)<0.0001?vec3(0,1,0):normalize(up);
  vec3 y=normalize(cross(z,x)); z=normalize(cross(x,y));
  return nb_matrix_quaternion(x,y,z);
 }
 vec3 y=dot(side,side)<0.0001?vec3(0,1,0):normalize(side);
 vec3 z=cross(x,y); z=dot(z,z)<0.0000000001?vec3(0):normalize(z);
 y=cross(z,x); y=dot(y,y)<0.0000000001?vec3(0):normalize(y);
 return nb_matrix_quaternion(x,y,z);
}
vec3 nb_cone(inout Particle p,float angle,float inner_angle,vec3 axis,float speed) {
 float cosine=nb_range(p,cos(radians(angle*0.5)),cos(radians(inner_angle*0.5)));
 float azimuth=nb_range(p,0.0,6.283185307179586);
 float sine=sqrt(max(1.0-cosine*cosine,0.0));
 vec3 direction=vec3(cos(azimuth)*sine,sin(azimuth)*sine,cosine);
 if(dot(axis,axis)<=0.0000000001) return vec3(0.0);
 axis=normalize(axis);
 if(axis.z < -0.999999) direction=vec3(direction.x,-direction.y,-direction.z);
 else { vec3 k=cross(vec3(0,0,1),axis); direction+=cross(k,direction)+cross(k,cross(k,direction))/(1.0+axis.z); }
 return direction*speed;
}
Particle nb_load(uint base) {
 Particle p;
 p.position=particles.data[base].xyz; p.age=particles.data[base].w;
 p.velocity=particles.data[base+1].xyz; p.lifetime=particles.data[base+1].w;
 p.alignment=particles.data[base+2].xyz; p.rotation=particles.data[base+2].w;
 p.size=particles.data[base+3].xy; p.initial_size=particles.data[base+3].zw;
 p.color=particles.data[base+4]; p.initial_color=particles.data[base+5]; p.dynamic_value=particles.data[base+6];
 p.mass=particles.data[base+7].x; p.uv_flip=particles.data[base+7].y; p.subimage=particles.data[base+7].z; p.material_random=particles.data[base+7].w;
 p.force=particles.data[base+8].xyz; p.drag=particles.data[base+8].w;
 p.previous_velocity=particles.data[base+9].xyz; p.alive=particles.data[base+9].w;
 p.presolve_position=particles.data[base+10].xyz; p.birth_time=particles.data[base+10].w;
 p.presolve_velocity=particles.data[base+11].xyz; p.id=particles.data[base+11].w;
 p.presolve_force=particles.data[base+12].xyz; p.rotational_drag=particles.data[base+12].w;
 p.advection=particles.data[base+13].xyz; p.ignore_mass=particles.data[base+13].w>0.5;
 p.size_factor=particles.data[base+14].xy; p.seed=floatBitsToUint(particles.data[base+14].z); p.cache_mask=floatBitsToUint(particles.data[base+14].w);
 p.normalized_age=particles.data[base+15].w;
 p.camera_offset=particles.data[base+16].w;
 p.spawn_group=floatBitsToInt(particles.data[base+17].w);
 p.birth_linear=mat3(particles.data[base+15].xyz,particles.data[base+16].xyz,particles.data[base+17].xyz);
 p.birth_rotation=mat3(particles.data[base+18].xyz,particles.data[base+19].xyz,particles.data[base+20].xyz);
 p.birth_origin=vec3(particles.data[base+18].w,particles.data[base+19].w,particles.data[base+20].w);
 for(int i=0;i<16;i++) p.cache[i]=particles.data[base+21+i];
 p.mesh_scale=particles.data[base+37].xyz; p.mesh_index=particles.data[base+37].w;
 p.initial_mesh_scale=particles.data[base+38].xyz; p.uv_flip_y=particles.data[base+38].w; p.mesh_scale_factor=vec3(1);
 p.mesh_orientation=particles.data[base+39]; p.initial_mesh_orientation=particles.data[base+40];
 p.initial_rotation=particles.data[base+41]; p.initial_position=particles.data[base+42].xyz; p.first_frame=particles.data[base+42].w>0.5;
 p.dynamic_value1=particles.data[base+43]; p.dynamic_value2=particles.data[base+44]; p.dynamic_value3=particles.data[base+45];
 p.spawn_execution=particles.data[base+46].xy; p.update_execution=particles.data[base+46].zw;
 /*NB_ATTRIBUTE_LOAD*/
 return p;
}
void nb_store(uint base,Particle p) {
 particles.data[base]=vec4(p.position,p.age); particles.data[base+1]=vec4(p.velocity,p.lifetime);
 particles.data[base+2]=vec4(p.alignment,p.rotation); particles.data[base+3]=vec4(p.size,p.initial_size);
 particles.data[base+4]=p.color; particles.data[base+5]=p.initial_color; particles.data[base+6]=p.dynamic_value;
 particles.data[base+7]=vec4(p.mass,p.uv_flip,p.subimage,p.material_random);
 particles.data[base+8]=vec4(p.force,p.drag); particles.data[base+9]=vec4(p.previous_velocity,p.alive);
 particles.data[base+10]=vec4(p.presolve_position,p.birth_time); particles.data[base+11]=vec4(p.presolve_velocity,p.id);
 particles.data[base+12]=vec4(p.presolve_force,p.rotational_drag); particles.data[base+13]=vec4(p.advection,p.ignore_mass?1.0:0.0);
 particles.data[base+14]=vec4(p.size_factor,uintBitsToFloat(p.seed),uintBitsToFloat(p.cache_mask));
 particles.data[base+15].w=p.normalized_age;
 particles.data[base+16].w=p.camera_offset;
 particles.data[base+17].w=intBitsToFloat(p.spawn_group);
 for(int i=0;i<16;i++) particles.data[base+21+i]=p.cache[i];
 particles.data[base+37]=vec4(p.mesh_scale,p.mesh_index);
 particles.data[base+38]=vec4(p.initial_mesh_scale,p.uv_flip_y);
 particles.data[base+39]=p.mesh_orientation; particles.data[base+40]=p.initial_mesh_orientation;
 particles.data[base+41]=p.initial_rotation; particles.data[base+42]=vec4(p.initial_position,p.first_frame?1.0:0.0);
 particles.data[base+43]=p.dynamic_value1; particles.data[base+44]=p.dynamic_value2; particles.data[base+45]=p.dynamic_value3;
 /*NB_ATTRIBUTE_STORE*/
}
/*NB_HELPERS*/
void nb_spawn(inout Particle p,float dt,float time) {
 bool spawning=true;
 p.execution=p.spawn_execution;
 /*NB_SPAWN*/
}
void nb_update(inout Particle p,float dt,float time) {
 bool spawning=false;
 /*NB_UPDATE*/
}
void main() {
 uint index=gl_GlobalInvocationID.x;
 if(index>=uint(params.data[0].w)) return;
 uint base=index*/*NB_STRIDE*/; Particle p=nb_load(base);
 bool newborn=p.alive<0.0;
 p.spawned_this_dispatch=newborn;
 if(newborn) { p.alive=1.0; nb_spawn(p,0.0,p.birth_time); p.initial_mesh_scale=p.mesh_scale; p.initial_mesh_orientation=p.mesh_orientation; p.initial_position=p.position; p.initial_color=p.color; p.initial_size=p.size; /*NB_ATTRIBUTE_INITIAL*/ }
 float frame_delta=params.data[0].x; float time=params.data[0].y;
 if(frame_delta>0.0 /*NB_BIRTH_UPDATE*/) {
  float dt=clamp(time-p.birth_time,0.0,frame_delta);
  p.force=vec3(0); p.drag=0.0; p.rotational_drag=0.0;
  p.ignore_mass=true; p.advection=vec3(0); p.size_factor=vec2(1); p.mesh_scale_factor=vec3(1);
  p.execution=newborn?p.spawn_execution:p.update_execution;
  nb_update(p,dt,time);
 }
 nb_store(base,p);
 // Native Niagara AcquireIndex: prefix within the active hardware wave,
 // atomic allocation across waves. Cross-wave order is deliberately unspecified.
 uvec4 mask=subgroupBallot(p.alive>0.5);
 uint active_count=subgroupBallotBitCount(mask);
 uint first=0u;
 if(subgroupElect()) first=atomicAdd(output_count.count,active_count);
 uint offset=subgroupBroadcastFirst(first);
 float output_index=p.alive>0.5?float(offset+subgroupBallotExclusiveBitCount(mask)):-1.0;
 particles.data[base+47]=vec4(output_index,p.execution,float(gl_SubgroupSize));
}
"""
