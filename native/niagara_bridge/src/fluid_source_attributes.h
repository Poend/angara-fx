#pragma once

namespace godot::nbfluid {
inline constexpr char source_module[] = "/NiagaraFluids/Modules/Particles/SetFluidSourceAttributes@3.0";
struct Field {
    const char *name;
    const char *type;
    const char *pin;
};
// Niagara updates the historical radius before replacing the current radius.
inline constexpr Field source_fields[] = {
    {"Particles.fluids_source_previous_radius", "float", nullptr},
    {"Particles.fluids_source_radius", "float", "Radius"},
    {"Particles.fluids_source_velocity", "vec3", "Velocity"},
    {"Particles.fluids_source_density", "float", "Density"},
    {"Particles.fluids_source_temperature", "float", "Temperature"},
    {"Particles.fluids_source_divergence", "float", "Divergence"},
    {"Particles.fluids_source_color", "vec4", "Color"},
};
} // namespace godot::nbfluid
