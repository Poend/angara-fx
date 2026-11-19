#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <functional>
#include <vector>
#include <type_traits>
namespace godot {
// Retained numerical read view. Compile handles on the owner thread, then read
// without projecting whole particles. Factories retain immutable schema/columns
// or COW bytes. No buffer owns a callback that retains that same buffer/view.
class NBNativeParticleView : public RefCounted {
    GDCLASS(NBNativeParticleView, RefCounted)
    struct Reader {
        Variant key; std::function<Variant(int64_t)> value; bool metadata_fallback = false, attribute_column = false;
        int native_kind = 0; std::function<void(int64_t,void *)> native;
    };
    template<class T> static constexpr int native_kind() {
        if constexpr(std::is_same_v<T,double>) return 1;
        else if constexpr(std::is_same_v<T,Vector2>) return 2;
        else if constexpr(std::is_same_v<T,Vector3>) return 3;
        else if constexpr(std::is_same_v<T,Color>) return 4;
        else if constexpr(std::is_same_v<T,Quaternion>) return 5;
        else return 0;
    }
    Array metadata;
    Variant attributes_key = String("attributes");
    HashMap<StringName, int64_t> fields, attributes, bindings;
    std::vector<Reader> readers;
    std::vector<int64_t> attribute_readers;
    TypedArray<String> errors;
    int64_t count = 0;
    bool valid = true;
    Variant metadata_value(int64_t lane, const Variant &key, bool attribute) const;
    Dictionary attribute_map(int64_t lane) const;
  protected:
    static void _bind_methods();
  public:
    void initialize(const Array &values);
    void add_field(const String &name, std::function<Variant(int64_t)> read);
    void add_attribute(const Variant &name, std::function<Variant(int64_t)> read);
    void add_error(const String &error) { errors.append(error); valid = false; }
    // Internal typed readers share the same retained data/public projection.
    // No Variant construction occurs when the requested native type matches.
    template<class T,class F> void add_native_field(const String &name,F read) {
        static_assert(native_kind<T>()!=0);
        add_field(name,[read](int64_t lane)->Variant {return read(lane);});
        auto &reader=readers[size_t(field_handle(name))];reader.native_kind=native_kind<T>();
        reader.native=[read](int64_t lane,void *out){*static_cast<T *>(out)=read(lane);};
    }
    template<class T> T read_native(int64_t handle,int64_t lane) const {
        static_assert(native_kind<T>()!=0);
        if(valid && lane>=0 && lane<count && handle>=0 && size_t(handle)<readers.size()) {
            const auto &reader=readers[size_t(handle)];
            if(reader.native_kind==native_kind<T>()) {T result;reader.native(lane,&result);return result;}
        }
        return T(read(handle,lane));
    }
    int64_t field_handle(const String &name);
    int64_t attribute_handle(const String &name);
    int64_t binding_handle(const String &name, const Dictionary &builtins);
    Variant read(int64_t handle, int64_t lane) const;
    bool has_field(int64_t handle, int64_t lane) const;
    bool has_attribute(int64_t handle, int64_t lane) const;
    Variant attribute_or(int64_t handle,int64_t lane,const Variant &fallback) const;
    int64_t get_count() const { return count; }
    TypedArray<String> get_errors() const { return errors.duplicate(); }
    static Ref<NBNativeParticleView> for_emitter(const Dictionary &emitter);
    static bool stock_player_method(Node *player, const String &method);
    static TypedArray<String> materialize(Dictionary emitter);
    static int64_t particle_count(const Dictionary &emitter);
    static int64_t emitter_capacity(const Dictionary &plan, int64_t player_limit);
    static void clear_particles(Dictionary emitter);
};
} // namespace godot
