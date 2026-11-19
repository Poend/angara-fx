#pragma once
#include <godot_cpp/classes/resource.hpp>

namespace godot::nbreader {
// Lower a straight-line emitted module with an indexed vector read and explicit particle writes.
Dictionary lower(const String &body, const String &map, const String &alias);
Dictionary bind(const Dictionary &extracted, const Ref<Resource> &stage, const Array &interfaces);
String pack(const Array &columns, const Array &emitters, PackedByteArray &packet);
} // namespace godot::nbreader
