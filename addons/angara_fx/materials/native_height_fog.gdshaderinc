// UE5.8 HeightFogCommon.ush and FogRendering.cpp ordinary two-layer height fog.
// This is linear integrated scattering/transmittance, before view pre-exposure.
uniform bool nb_height_fog_enabled = false;
uniform vec4 nb_fog_layer0 = vec4(0.0);
uniform vec4 nb_fog_layer1 = vec4(0.0);
uniform vec4 nb_fog_distances = vec4(0.0,0.0,0.0,1.0);
uniform vec3 nb_fog_scattering = vec3(0.0);
uniform bool nb_fog_ortho_adjustment = true;
uniform float nb_fog_camera_target_distance = 0.0;
uniform vec4 nb_fog_direction = vec4(0.0,1.0,0.0,-1.0);
uniform vec4 nb_fog_directional_color = vec4(0.0,0.0,0.0,4.0);
varying vec4 nb_height_fog_vertex;

float nb_fog_origin(vec4 layer, float height) {
    return layer.x * exp2(clamp(-layer.y * (height-layer.z),-125.0,126.0));
}
float nb_fog_integral(float falloff, float vertical_delta, float origin_density) {
    float f = max(-127.0,falloff*vertical_delta);
    float transfer = abs(f)>0.01 ? (1.0-exp2(-f))/f : log(2.0)-0.5*log(2.0)*log(2.0)*f;
    return origin_density*transfer;
}
vec4 nb_evaluate_height_fog(vec3 receiver_world_m) {
    vec3 camera = CAMERA_POSITION_WORLD*100.0;
    vec3 ray = (receiver_world_m-CAMERA_POSITION_WORLD)*100.0;
    float max_difference = 65536.0;
    bool ortho_adjust = nb_fog_ortho_adjustment && PROJECTION_MATRIX[3][3]==1.0;
    if (ortho_adjust) {
        max_difference = max(camera.y,nb_fog_camera_target_distance*abs(INV_VIEW_MATRIX[2].y));
    }
    float max_height = 3.402823466e38;
    if (nb_fog_layer0.x>0.0) max_height=min(max_height,nb_fog_layer0.z+max_difference);
    if (nb_fog_layer1.x>0.0) max_height=min(max_height,nb_fog_layer1.z+max_difference);
    float observer = min(camera.y,max_height);
    float origin_height = ortho_adjust ? max_height : observer;
    float origin0 = nb_fog_origin(nb_fog_layer0,origin_height);
    float origin1 = nb_fog_origin(nb_fog_layer1,origin_height);
    float horizontal_squared = dot(ray.xz,ray.xz);
    if (nb_fog_distances.y>0.0 && horizontal_squared>nb_fog_distances.y*nb_fog_distances.y) {
        ray*=nb_fog_distances.y/sqrt(max(1.0,horizontal_squared));
    }
    ray.y+=camera.y-observer;
    float squared = dot(ray,ray);
    float inverse_length = inversesqrt(max(squared,0.00000001));
    float distance = squared*inverse_length;
    vec3 ray_direction = ray*inverse_length;
    float length_after_start = distance;
    float vertical_delta = ray.y;
    if (nb_fog_distances.x>0.0) {
        float fraction = nb_fog_distances.x*inverse_length;
        float intersection_height = observer+fraction*ray.y;
        length_after_start = (1.0-fraction)*distance;
        vertical_delta = ray.y*(1.0-fraction);
        origin0 = nb_fog_layer0.x*exp2(-max(-127.0,nb_fog_layer0.y*(intersection_height-nb_fog_layer0.z)));
        origin1 = nb_fog_layer1.x*exp2(-max(-127.0,nb_fog_layer1.y*(intersection_height-nb_fog_layer1.z)));
    }
    float shared_integral = nb_fog_integral(nb_fog_layer0.y,vertical_delta,origin0)+nb_fog_integral(nb_fog_layer1.y,vertical_delta,origin1);
    float transmittance = max(clamp(exp2(-shared_integral*length_after_start),0.0,1.0),1.0-nb_fog_distances.w);
    vec3 directional = vec3(0.0);
    if (nb_fog_direction.w>=0.0) {
        float lobe = pow(clamp(dot(ray_direction,nb_fog_direction.xyz),0.0,1.0),nb_fog_directional_color.w);
        float directional_transmittance = clamp(exp2(-shared_integral*max(length_after_start-nb_fog_direction.w,0.0)),0.0,1.0);
        directional = nb_fog_directional_color.rgb*lobe*(1.0-directional_transmittance);
    }
    if (nb_fog_distances.z>0.0 && distance>nb_fog_distances.z) {
        transmittance=1.0;
        directional=vec3(0.0);
    }
    return vec4(nb_fog_scattering*(1.0-transmittance)+directional,transmittance);
}
