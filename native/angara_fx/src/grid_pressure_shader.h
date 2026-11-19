#pragma once

namespace godot::nbgrid {
// Input storage and pressure belong to the previous iteration. The caller supplies
// the center values from the Niagara stage's named transient attributes.
inline constexpr const char *pressure_kernel = R"GLSL(
layout(set=0,binding=3) uniform sampler3D boundary_volume;
float solve_pressure(ivec3 pos, ivec3 size, float spacing, float dt, float divergence,
                     int iteration, float center, float boundary, vec3 velocity, float relaxation) {
    if (int(roundEven(boundary)) != 0) return 0.0;
    float neighbors = 0.0;
    float flux = 0.0;
    int divisor = 6;
    for (int axis = 0; axis < 3; ++axis) {
        for (int side = 1; side >= -1; side -= 2) {
            ivec3 other = pos;
            other[axis] += side;
            bool inside = all(greaterThanEqual(other, ivec3(0))) && all(lessThan(other, size));
            int cell_type = inside ? int(roundEven(texelFetch(boundary_volume, other, 0).r)) : 0;
            if (cell_type == 1) {
                --divisor;
                flux += float(side) * (spacing / dt) * velocity[axis];
            } else if (cell_type != 2) {
                neighbors += read_cell(other, size);
            }
        }
    }
    if (divisor == 0) return 0.0;
    float next_pressure = (neighbors - spacing * spacing * divergence / dt + flux) / float(divisor);
    float r = clamp(relaxation, 0.0, 1.0);
    float weight = r < 1e-8 ? 1.0 : float((pos.x+pos.y+pos.z+iteration+1)&1) * min(1.93, 1.0+r);
    return (1.0-weight)*center + weight*next_pressure;
}
)GLSL";
} // namespace godot::nbgrid
