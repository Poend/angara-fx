#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>
namespace godot {
class NBNativeVMEvents : public RefCounted {
    GDCLASS(NBNativeVMEvents, RefCounted)
    Dictionary current, previous;

  protected:
    static void _bind_methods();

  public:
    static Dictionary graph(const Array &plans);
    void begin_frame();
    Dictionary read(const String &receiver, const Dictionary &settings) const;
    TypedArray<String> publish(const String &emitter, const Dictionary &events);
    Dictionary get_current() const {
        return current;
    }
    Dictionary get_previous() const {
        return previous;
    }
};
} // namespace godot
