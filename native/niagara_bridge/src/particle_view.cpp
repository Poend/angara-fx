#include "particle_view.h"
#include "particle_buffer.h"
#include "gpu_buffers.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/script.hpp>
#include <algorithm>
namespace godot {
void NBNativeParticleView::_bind_methods() {
    ClassDB::bind_static_method("NBNativeParticleView",D_METHOD("particle_count","emitter"),&NBNativeParticleView::particle_count);
    ClassDB::bind_method(D_METHOD("field_handle", "name"), &NBNativeParticleView::field_handle);
    ClassDB::bind_method(D_METHOD("attribute_handle", "name"), &NBNativeParticleView::attribute_handle);
    ClassDB::bind_method(D_METHOD("binding_handle", "name", "builtins"), &NBNativeParticleView::binding_handle);
    ClassDB::bind_method(D_METHOD("read", "handle", "lane"), &NBNativeParticleView::read);
    ClassDB::bind_method(D_METHOD("get_count"), &NBNativeParticleView::get_count);
    ClassDB::bind_method(D_METHOD("get_errors"), &NBNativeParticleView::get_errors);
    ClassDB::bind_static_method("NBNativeParticleView",D_METHOD("stock_player_method","player","method"),&NBNativeParticleView::stock_player_method);
    ClassDB::bind_static_method("NBNativeParticleView",D_METHOD("materialize","emitter"),&NBNativeParticleView::materialize);
}
void NBNativeParticleView::initialize(const Array &values) {
    metadata = values;
    count = values.size(); valid = true;
    fields.clear(); attributes.clear(); bindings.clear(); readers.clear(); attribute_readers.clear(); errors.clear();
    add_field("attributes", [this](int64_t lane) { return attribute_map(lane); });
}
Variant NBNativeParticleView::metadata_value(int64_t lane, const Variant &key, bool attribute) const {
    Dictionary row = metadata[lane];
    if(!attribute) return row.get(key,Variant());
    Variant raw=row.get(attributes_key,Variant());
    return raw.get_type()==Variant::DICTIONARY ? Dictionary(raw).get(key,Variant()) : Variant();
}
void NBNativeParticleView::add_field(const String &name, std::function<Variant(int64_t)> read) {
    StringName key(name);
    if (auto *handle = fields.getptr(key)) {
        auto &reader=readers[size_t(*handle)];reader.value=std::move(read);
        reader.native_kind=0;reader.native={};
    }
    else { fields[key] = int64_t(readers.size()); readers.push_back({name, std::move(read)}); }
}
void NBNativeParticleView::add_attribute(const Variant &name, std::function<Variant(int64_t)> read) {
    StringName key{String(name)};
    if (auto *handle = attributes.getptr(key)) readers[size_t(*handle)].value = std::move(read);
    else {
        int64_t index = int64_t(readers.size());
        attributes[key] = index; attribute_readers.push_back(index); readers.push_back({name, std::move(read)});
        readers.back().attribute_column=true;
    }
}
int64_t NBNativeParticleView::field_handle(const String &name) {
    if (auto *handle = fields.getptr(StringName(name))) return *handle;
    add_field(name, [this, key = Variant(name)](int64_t lane) { return metadata_value(lane, key, false); });
    readers.back().metadata_fallback = true;
    return *fields.getptr(StringName(name));
}
int64_t NBNativeParticleView::attribute_handle(const String &name) {
    if (auto *handle = attributes.getptr(StringName(name))) return *handle;
    // Missing attributes are metadata fallbacks, not new keys in attributes().
    int64_t handle = int64_t(readers.size());
    attributes[StringName(name)] = handle;
    readers.push_back({name, [this, key = Variant(name)](int64_t lane) { return metadata_value(lane, key, true); }});
    return handle;
}
int64_t NBNativeParticleView::binding_handle(const String &name, const Dictionary &builtins) {
    // Builtins may differ per caller; cache only the two invariant derived reads.
    if (name == "Particles.SpriteRotation" || name == "Particles.UVScale") {
        if (auto *handle = bindings.getptr(StringName(name))) return *handle;
        int64_t first = field_handle(name == "Particles.SpriteRotation" ? "rotation" : "uv_flip");
        int64_t second = name == "Particles.UVScale" ? field_handle("uv_flip_y") : -1;
        bool first_fallback = readers[size_t(first)].metadata_fallback;
        bool second_fallback = second >= 0 && readers[size_t(second)].metadata_fallback;
        int64_t handle = int64_t(readers.size());
        readers.push_back({name, [this, first, second, first_fallback, second_fallback](int64_t lane) -> Variant {
            if (second < 0) return Math::rad_to_deg(double(read(first, lane)));
            Dictionary row = metadata[lane];
            Variant x = first_fallback && !row.has("uv_flip") ? Variant(1.) : read(first,lane);
            Variant y = second_fallback && !row.has("uv_flip_y") ? Variant(1.) : read(second,lane);
            return Vector2(x,y);
        }});
        bindings[StringName(name)] = handle;
        return handle;
    }
    if (builtins.has(name)) {
        Array row = builtins[name];
        return row.size() > 1 ? field_handle(row[1]) : -1;
    }
    return attribute_handle(name);
}
Variant NBNativeParticleView::read(int64_t handle, int64_t lane) const {
    if (!valid || lane < 0 || lane >= count || handle < 0 || size_t(handle) >= readers.size()) return Variant();
    Variant value = readers[size_t(handle)].value(lane);
    if (value.get_type() == Variant::ARRAY) return Array(value).duplicate(true);
    if (value.get_type() == Variant::DICTIONARY) return Dictionary(value).duplicate(true);
    return value;
}
bool NBNativeParticleView::has_field(int64_t handle, int64_t lane) const {
    if (!valid || lane < 0 || lane >= count || handle < 0 || size_t(handle) >= readers.size()) return false;
    return !readers[size_t(handle)].metadata_fallback || Dictionary(metadata[lane]).has(readers[size_t(handle)].key);
}
bool NBNativeParticleView::has_attribute(int64_t handle, int64_t lane) const {
    if (!valid || lane < 0 || lane >= count || handle < 0 || size_t(handle) >= readers.size()) return false;
    if (readers[size_t(handle)].attribute_column) return true;
    Variant raw=Dictionary(metadata[lane]).get(attributes_key,Variant());
    return raw.get_type()==Variant::DICTIONARY && Dictionary(raw).has(readers[size_t(handle)].key);
}
Variant NBNativeParticleView::attribute_or(int64_t handle,int64_t lane,const Variant &fallback) const {
    if(!valid || lane<0 || lane>=count || handle<0 || size_t(handle)>=readers.size()) return fallback;
    const auto &reader=readers[size_t(handle)];
    if(reader.attribute_column) return read(handle,lane);
    Variant raw=Dictionary(metadata[lane]).get(attributes_key,Variant());
    if(raw.get_type()!=Variant::DICTIONARY) return fallback;
    Variant value=Dictionary(raw).get(reader.key,fallback);
    if(value.get_type()==Variant::ARRAY) return Array(value).duplicate(true);
    if(value.get_type()==Variant::DICTIONARY) return Dictionary(value).duplicate(true);
    return value;
}
Dictionary NBNativeParticleView::attribute_map(int64_t lane) const {
    Dictionary result = Dictionary(Dictionary(metadata[lane]).get("attributes", Dictionary())).duplicate(true);
    for (int64_t handle : attribute_readers) result[readers[size_t(handle)].key] = read(handle, lane);
    return result;
}
Ref<NBNativeParticleView> NBNativeParticleView::for_emitter(const Dictionary &emitter) {
    Array particles = emitter.get("particles", Array());
    Ref<NBNativeGPUParticleBuffer> gpu = emitter.get("gpu_buffer", Variant());
    if(gpu.is_valid() && bool(emitter.get("gpu_births_only",false)) && bool(emitter.get("particle_projection_pending",false)))
        return particles.is_empty() ? gpu->get_view() : Ref<NBNativeParticleView>();
    if (gpu.is_valid() && gpu->get_count() == particles.size()) return gpu->get_view();
    Ref<NBNativeParticleBuffer> cpu = emitter.get("vm_buffer", Variant());
    if (cpu.is_valid() && cpu->get_count() == particles.size()) return cpu->get_view();
    return Ref<NBNativeParticleView>();
}
bool NBNativeParticleView::stock_player_method(Node *player,const String &method) {
    if(!player) return false;
    Ref<Script> current=player->get_script();
    while(current.is_valid()) {
        // The exact stock GDScript declares these entry points itself. Avoid
        // allocating hundreds of reflection dictionaries in the common case.
        // Derived/custom scripts retain the original declaring-script lookup:
        // has_script_method also sees inherited methods and is insufficient there.
        if(current->is_class("GDScript") && current->get_path()=="res://addons/niagara_bridge/runtime/effect_player.gd")
            return current->has_script_method(method);
        TypedArray<Dictionary> methods=current->get_script_method_list();
        for(int64_t i=0;i<methods.size();++i) if(String(Dictionary(methods[i]).get("name",""))==method)
            return false;
        current=current->get_base_script();
    }
    return false;
}
int64_t NBNativeParticleView::emitter_capacity(const Dictionary &plan, int64_t player_limit) {
    const int64_t source_limit = plan.get("max_instance_count", player_limit);
    return std::max<int64_t>(0, std::min(source_limit, player_limit));
}
int64_t NBNativeParticleView::particle_count(const Dictionary &emitter) {
    Array particles=emitter.get("particles",Array());
    if(bool(emitter.get("gpu_births_only",false)) && bool(emitter.get("particle_projection_pending",false))) {
        Ref<NBNativeGPUParticleBuffer> gpu=emitter.get("gpu_buffer",Variant());
        if(gpu.is_valid()) return gpu->get_count()+particles.size();
    }
    return particles.size();
}
void NBNativeParticleView::clear_particles(Dictionary emitter) {
    Array particles=emitter.get("particles",Array());particles.clear();
    if(bool(emitter.get("gpu_births_only",false))) {
        emitter.erase("gpu_births_only");emitter["particle_projection_pending"]=false;
    }
}
TypedArray<String> NBNativeParticleView::materialize(Dictionary emitter) {
    TypedArray<String> errors;
    if(!bool(emitter.get("particle_projection_pending",false))) return errors;
    Ref<NBNativeGPUParticleBuffer> gpu=emitter.get("gpu_buffer",Variant());
    Ref<NBNativeParticleBuffer> cpu=emitter.get("vm_buffer",Variant());
    if(gpu.is_null() && cpu.is_null()) {errors.append("Pending particle projection has no numerical buffer.");return errors;}
    if(bool(emitter.get("gpu_births_only",false)) && gpu.is_null()) {
        errors.append("Pending GPU births have no numerical GPU buffer.");return errors;
    }
    int64_t count=gpu.is_valid() ? gpu->get_count() : cpu->get_count();
    Array current=emitter.get("particles",Array());
    bool births_only=bool(emitter.get("gpu_births_only",false));
    if (count == 0 && current.is_empty()) {
        emitter["particle_projection_pending"] = false;
        emitter.erase("gpu_births_only");
        return errors;
    }
    if(!births_only && current.is_empty() && count>0) {
        emitter["particle_projection_pending"]=false;return errors;
    }
    if(!births_only && current.size()!=count) {
        errors.append("Pending particle projection lane count differs from its numerical buffer.");return errors;
    }
    Dictionary projected=gpu.is_valid() ? gpu->snapshot() : cpu->snapshot(false);errors.append_array(projected["errors"]);
    if(errors.is_empty()) {
        Array particles=projected["particles"];
        if(cpu.is_valid() && gpu.is_null()) for(int64_t i=0;i<particles.size();++i) Dictionary(particles[i]).erase("vm_needs_spawn");
        if(births_only) particles.append_array(current);
        emitter["particles"]=particles;emitter["particle_projection_pending"]=false;emitter.erase("gpu_births_only");
    }
    return errors;
}
} // namespace godot
