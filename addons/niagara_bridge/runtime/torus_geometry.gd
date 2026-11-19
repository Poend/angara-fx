@tool
extends NBNativeTorusGeometry
## UE ParametricTorus/ParametricTorusKnot and legacy TorusLocation geometry.
const Math=preload("res://addons/niagara_bridge/runtime/niagara_cpu_math.gd")
const GPU_SOURCE = """
void nb_torus_geometry(int mode,float radial,float u,float v,float c,float minor,float q,float loops_p,bool tube,float tube_radius,out vec3 position,out vec3 tangent,out vec3 normal){
 const float tau=6.283185307179586;
 if(mode==1){vec2 xy=vec2(cos(u*tau),sin(u*tau))*(sqrt(radial)*c);position=vec3(xy,1.0);normal=normalize(position);tangent=cross(vec3(0,0,1),normal);return;}
 float a=minor*(mode==2?radial:sqrt(radial));
 float uu=(mode==2?u*q:u)*tau;float vv=(mode==2?u*loops_p:v)*tau;
 float cosu=cos(uu),sinu=sin(uu),cosv=cos(vv),sinv=sin(vv);
 precise float width=c+a*cosv;position=vec3(width*cosu,width*sinu,a*sinv);
 if(mode==2){
  float ap_sin=a*loops_p*sinv;
  tangent=normalize(vec3(-q*width*sinu-ap_sin*cosu,q*width*cosu-ap_sin*sinu,a*loops_p*cosv));
  float apq_sin=a*loops_p*q*sinv;float aq2=a*(q*q);float ap2=a*(loops_p*loops_p);float cq2=c*(q*q);
  normal=normalize(vec3(-(2.0*apq_sin*sinu+((-aq2-ap2)*cosv-cq2)*cosu),-(-((aq2+ap2)*cosv+cq2)*sinu-2.0*apq_sin*cosu),ap2*sinv));
  if(tube){vec3 bitangent=cross(normal,tangent);vec2 circle=vec2(cos(v*tau),sin(v*tau))*tube_radius;vec3 displacement=bitangent*circle.x+normal*circle.y;position+=displacement;normal=normalize(displacement);}
 }else{tangent=vec3(-sinu,cosu,0);normal=cross(tangent,vec3(-sinv*cosu,-sinv*sinu,cosv));}
}
vec3 nb_torus_rotate(vec3 v,vec4 q){vec3 t=2.0*cross(q.xyz,v);return v+q.w*t+cross(q.xyz,t);}
void nb_torus_orient(vec3 axis,float turns,inout vec3 position,inout vec3 tangent,inout vec3 normal){
 axis=dot(axis,axis)<1e-10?vec3(0,0,1):normalize(axis);
 float norm=sqrt(dot(axis,axis));float w=norm+axis.z;
 vec4 align=w>=1e-6*norm?vec4(-axis.y,axis.x,0,w):vec4(0,-1,0,0);
 align=norm<1e-6?vec4(0,0,0,1):normalize(align);
 float half_angle=turns*3.141592653589793;vec4 spin=vec4(0,0,sin(half_angle),cos(half_angle));
 vec4 rotation=vec4(align.w*spin.xyz+spin.w*align.xyz+cross(align.xyz,spin.xyz),align.w*spin.w-dot(align.xyz,spin.xyz));
 position=nb_torus_rotate(position,rotation);tangent=nb_torus_rotate(tangent,rotation);normal=nb_torus_rotate(normal,rotation);
}
"""

