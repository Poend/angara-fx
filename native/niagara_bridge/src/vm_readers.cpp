#include "vm_program.h"
#include "vm_scalar.h"
#include "particle_buffer.h"
#include <godot_cpp/core/class_db.hpp>
using namespace godot;
namespace {
Dictionary materialize_reader(Dictionary value) {
    if(value.has("resident_buffer")) {
        Ref<NBNativeParticleBuffer> buffer=value["resident_buffer"];
        if(buffer.is_valid()) {
            Array particles=Dictionary(buffer->snapshot(false))["particles"];
            for(int64_t i=0;i<particles.size();++i) Dictionary(particles[i]).erase("vm_needs_spawn");
            value["particles"]=particles;
        }
        value.erase("resident_buffer");
    }
    return value;
}
Dictionary materialize_frames(Dictionary frames) {
    Array names=frames.keys();for(int64_t i=0;i<names.size();++i) materialize_reader(frames[names[i]]);
    return frames;
}
}
Dictionary NBNativeVMReaders::get_current() const {return materialize_frames(current);}
Dictionary NBNativeVMReaders::get_previous() const {return materialize_frames(previous);}
void NBNativeVMReaders::_bind_methods() {
    ClassDB::bind_method(D_METHOD("begin_frame"), &NBNativeVMReaders::begin_frame);
    ClassDB::bind_method(D_METHOD("publish", "name", "particles", "ids", "spawned_count"),
                         &NBNativeVMReaders::publish);
    ClassDB::bind_method(D_METHOD("publish_buffer","name","buffer","ids","spawned_count"),&NBNativeVMReaders::publish_buffer);
    ClassDB::bind_method(D_METHOD("source", "binding", "scope"), &NBNativeVMReaders::source);
    ClassDB::bind_method(D_METHOD("capture", "bindings", "scope"), &NBNativeVMReaders::capture);
    ClassDB::bind_method(D_METHOD("set_phase", "value"), &NBNativeVMReaders::set_phase);
    ClassDB::bind_method(D_METHOD("get_phase"), &NBNativeVMReaders::get_phase);
    ClassDB::bind_method(D_METHOD("get_current"), &NBNativeVMReaders::get_current);
    ClassDB::bind_method(D_METHOD("get_previous"), &NBNativeVMReaders::get_previous);
    ClassDB::bind_static_method("NBNativeVMReaders", D_METHOD("bind", "info", "function"),
                                &NBNativeVMReaders::bind);
    ClassDB::bind_static_method("NBNativeVMReaders", D_METHOD("sample", "binding", "inputs", "data"),
                                &NBNativeVMReaders::sample);
    ClassDB::bind_static_method("NBNativeVMReaders", D_METHOD("index_for_id", "data", "id", "tag"),
                                &NBNativeVMReaders::index_for_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "phase", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "set_phase", "get_phase");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "current", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_current");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "previous", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_previous");
}
void NBNativeVMReaders::begin_frame() {
    previous = current;
    current = Dictionary();
    phase = "system";
}
void NBNativeVMReaders::publish(const String &name, const Array &particles, const Ref<NBNativeVMIDs> &ids,
                                int spawned_count) {
    Dictionary value;
    value["particles"] = particles.duplicate();
    value["ids"] = ids.is_valid() ? ids->snapshot() : Dictionary();
    value["spawned_count"] = spawned_count;
    current[name] = value;
}
void NBNativeVMReaders::publish_buffer(const String &name,const Ref<NBNativeParticleBuffer> &buffer,
                                       const Ref<NBNativeVMIDs> &ids,int spawned_count) {
    Dictionary value;value["particles"]=Array();value["resident_buffer"]=buffer->duplicate_buffer();
    value["ids"]=ids.is_valid() ? ids->snapshot() : Dictionary();value["spawned_count"]=spawned_count;
    current[name]=value;
}
Dictionary NBNativeVMReaders::source(const Dictionary &binding, const String &scope) const {
    String name = binding.get("source_emitter", "");
    Dictionary frames = phase == "system" || (phase == "particles" && scope == name) ? previous : current;
    Dictionary empty;
    empty["particles"] = Array();
    empty["ids"] = Dictionary();
    empty["spawned_count"] = 0;
    return materialize_reader(frames.get(name,empty));
}
Dictionary NBNativeVMReaders::capture(const Dictionary &bindings, const String &scope) const {
    Dictionary result;
    Array keys = bindings.keys();
    for (int n = 0; n < keys.size(); n++) {
        Dictionary binding = bindings[keys[n]];
        if (binding.get("kind", "") != Variant("particle_reader") || !bool(binding.get("resolved", false)))
            continue;
        Dictionary data = source(binding, scope), snapshot;
        Array particles = data.get("particles", Array()), raw;
        for (int j = 0; j < particles.size(); j++) {
            Dictionary p = particles[j], row;
            row["_vm_fields"] = Dictionary(p.get("_vm_fields", Dictionary())).duplicate(true);
            raw.append(row);
        }
        snapshot["particles"] = raw;
        snapshot["ids"] = Dictionary(data.get("ids", Dictionary())).duplicate(true);
        snapshot["spawned_count"] = data.get("spawned_count", 0);
        result[binding.get("source_emitter", "")] = snapshot;
    }
    return result;
}
Dictionary NBNativeVMReaders::bind(const Dictionary &info, const Dictionary &function) {
    nbvm::ExternalBinding b;
    TypedArray<String> errors;
    Dictionary result;
    bool ok = b.bind(info, function, Dictionary(), errors);
    Dictionary binding = ok ? b.metadata : Dictionary();
    if (ok) {
        binding["_interface"] = info.duplicate(true);
        binding["_signature"] = function.duplicate(true);
    }
    result["errors"] = errors;
    result["binding"] = binding;
    return result;
}
Array NBNativeVMReaders::sample(const Dictionary &binding, const Array &inputs, const Dictionary &data) {
    nbvm::ExternalBinding b;
    TypedArray<String> errors;
    Array result;
    if (!b.bind(binding.get("_interface", Dictionary()), binding.get("_signature", Dictionary()),
                Dictionary(), errors) ||
        b.kind != nbvm::ExternalBinding::Reader || inputs.size() > 4)
        return result;
    nbvm::ExternalInputs words{};
    for (int n = 0; n < inputs.size(); n++)
        words[n + 1] = uint32_t(int64_t(inputs[n]));
    nbvm::ExternalOutputs output{};
    b.sample(words, data, output);
    String type = b.metadata.get("type", "");
    for (int n = 0; n < b.width; n++) {
        // Keep diagnostic helper zero literals compatible. The VM retains declared word types.
        bool default_integer_zero =
            n > 0 && output[0] == 0 && output[n] == 0 && type != "Color" && type != "Quaternion";
        result.append(b.integer_outputs[n]   ? Variant(nbvm::signed_word(output[n]))
                      : default_integer_zero ? Variant(0)
                                             : Variant(nbvm::number(output[n])));
    }
    return result;
}
int NBNativeVMReaders::index_for_id(const Dictionary &data, int id, int tag) {
    return nbvm::ExternalBinding::reader_index(data, id, tag);
}
