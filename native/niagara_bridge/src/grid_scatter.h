#pragma once
#include <godot_cpp/variant/variant.hpp>

namespace godot::nbgrid {
inline constexpr char scatter_module[] = "/NiagaraFluids/Modules/Grid3D/Grid3D_Gas_ParticleScatterSource@1.0";
String scatter_source(const Dictionary &layout, const Dictionary &pins, const Dictionary &named_indices,
                      Dictionary &stage);
String scatter_particles(const Dictionary &stage, const Array &emitters, const Dictionary &context,
                         PackedByteArray &packet);
} // namespace godot::nbgrid
