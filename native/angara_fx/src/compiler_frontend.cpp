#include "compiler_frontend.h"
#include "fluid_source_attributes.h"
#include "grid_stages.h"
#include "attribute_values.h"
#include <godot_cpp/classes/reg_ex.hpp>
#include <godot_cpp/classes/reg_ex_match.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <cmath>
#include <cstring>

namespace godot {
namespace {
bool numeric(const Variant &v) { return v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT; }
bool equal(const Variant &a, const Variant &b) {
    Variant result; bool valid = false;
    Variant::evaluate(Variant::OP_EQUAL, a, b, result, valid);
    return valid && bool(result);
}
bool named(const Variant &v, std::initializer_list<const char *> names) {
    for (const char *name : names) if (equal(v, String(name))) return true;
    return false;
}
bool scalar_type(const Variant &v) { return named(v, {"float32", "int32", "float16"}); }
void issue(Dictionary layout, const Ref<Resource> &operation, const String &message) {
    Dictionary entry; entry["operation"] = operation; entry["message"] = message;
    Array errors = layout["errors"]; errors.append(entry);
}
Array stages(const Ref<Resource> &emitter) { return emitter->get("stages"); }
Array operations(const Ref<Resource> &stage) { return stage->get("operations"); }
bool particle_stage(const String &name) { return name == "ParticleSpawn" || name == "ParticleUpdate"; }
}

void NBNativeCompilerAttributes::_bind_methods() {
    ClassDB::bind_static_method("NBNativeCompilerAttributes", D_METHOD("value_type", "value", "known", "builtins", "dynamic_specs"), &NBNativeCompilerAttributes::value_type);
    ClassDB::bind_static_method("NBNativeCompilerAttributes", D_METHOD("assignment_error", "value", "type", "known", "builtins", "dynamic_specs"), &NBNativeCompilerAttributes::assignment_error);
    ClassDB::bind_static_method("NBNativeCompilerAttributes", D_METHOD("references", "value"), &NBNativeCompilerAttributes::references);
    ClassDB::bind_static_method("NBNativeCompilerAttributes", D_METHOD("module_outputs", "operation", "schemas"), &NBNativeCompilerAttributes::module_outputs);
    ClassDB::bind_static_method("NBNativeCompilerAttributes", D_METHOD("build", "emitter", "native_particles", "builtins", "schemas", "source_defaults"), &NBNativeCompilerAttributes::build, DEFVAL(Dictionary()));
}
String NBNativeCompilerAttributes::value_type(const Variant &v, const Dictionary &known, const Dictionary &builtins, const Dictionary &specs) {
    switch (v.get_type()) {
    case Variant::BOOL: return "bool";
    case Variant::INT: return "int";
    case Variant::FLOAT: return "float";
    case Variant::VECTOR2: return "vec2";
    case Variant::VECTOR3: return "vec3";
    case Variant::COLOR: return "color";
    case Variant::QUATERNION: return "quat";
    case Variant::VECTOR4: return "vec4";
    case Variant::ARRAY: {
        Array a = v;
        for (int i = 0; i < a.size(); ++i) if (!numeric(a[i]) || !std::isfinite(double(a[i]))) return "";
        return a.size() == 2 ? "vec2" : a.size() == 3 ? "vec3" : a.size() == 4 ? "vec4" : "";
    }
    case Variant::DICTIONARY: {
        Dictionary d = v;
        String name = d.get("name", "");
        if (equal(d.get("kind", Variant()), String("call")) && specs.has(name)) return Dictionary(specs[name])["type"];
        if (equal(d.get("kind", Variant()), String("reference"))) {
            if (builtins.has(name)) return Array(builtins[name])[0];
            return known.get(name, "");
        }
        if (!equal(d.get("kind", Variant()), String("call"))) return "";
        if (named(name, {"RandomRangeFloat", "FloatFromCurve", "ReturnNormalizedExecIndex", "Add_Float", "Multiply_Float", "MakeFloatFromVector2d"})) return "float";
        if (name == "RandomRangeInt") return "int";
        if (name == "ColorFromCurve") return "color";
        if (named(name, {"Vector2DFromCurve", "Vector2DFromFloat", "MakeVector2D"})) return "vec2";
        if (name == "AddVectorToPosition") return "position";
        if (named(name, {"RandomRangeVector", "VectorFromCurve", "VectorFromFloat", "MakeVector", "MakeVectorFromLinearColorRGB", "Multiply_Vector", "Multiply_VectorByFloat", "Add_Vector", "TransformVector"})) return "vec3";
        return "";
    }
    default: return "";
    }
}
String NBNativeCompilerAttributes::assignment_error(const Variant &v, const String &type, const Dictionary &known, const Dictionary &builtins, const Dictionary &specs) {
    String actual = value_type(v, known, builtins, specs);
    if (actual.is_empty()) return "Assignment expression has an unsupported/unresolved type.";
    if (NBNativeAttributeValues::compatible(actual, type) ||
        ((actual == "float" || actual == "int") && (type == "float" || type == "int")) || (type == "quat" && actual == "vec4")) {
        if (numeric(v) && (!std::isfinite(double(v)) || (type == "int" && (double(v) < -2147483648.0 || double(v) > 2147483647.0))))
            return "Assignment scalar is non-finite or outside signed32 storage.";
        return "";
    }
    return "Assignment expects " + type + " but expression has type " + actual + ".";
}
TypedArray<String> NBNativeCompilerAttributes::references(const Variant &v) {
    TypedArray<String> found;
    if (v.get_type() != Variant::DICTIONARY) return found;
    Dictionary d = v;
    if (equal(d.get("kind", Variant()), String("reference"))) found.append(String(d.get("name", "")));
    else if (equal(d.get("kind", Variant()), String("call"))) {
        Array args = d.get("arguments", Array());
        for (int i = 0; i < args.size(); ++i) {
            Dictionary argument = args[i];
            TypedArray<String> nested = references(argument.get("value", Variant()));
            for (int j = 0; j < nested.size(); ++j) if (!found.has(nested[j])) found.append(nested[j]);
        }
    }
    return found;
}
Dictionary NBNativeCompilerAttributes::module_outputs(const Ref<Resource> &op, const Dictionary &schemas) {
    Dictionary result;
    if (op.is_null()) return result;
    String module = op->get("module_name"), alias = op->get("alias_name");
    if (alias.is_empty()) alias = module.get_slice("@", 0).get_file();
    Dictionary fields = schemas.get(module, Dictionary());
    Array keys = fields.keys();
    for (int i = 0; i < keys.size(); ++i) result["Output." + alias + "." + String(keys[i])] = fields[keys[i]];
    return result;
}
Dictionary NBNativeCompilerAttributes::build(const Ref<Resource> &emitter, bool native, const Dictionary &builtins, const Dictionary &schemas, const Dictionary &source_defaults) {
    Dictionary layout, declarations, initial, outputs;
    Array particles, emitter_rows, errors;
    layout["declarations"] = declarations; layout["particle"] = particles; layout["emitter"] = emitter_rows;
    layout["initial"] = initial; layout["outputs"] = outputs; layout["errors"] = errors;
    if (emitter.is_null()) return layout;
    Array stage_list = stages(emitter);
    for (int s = 0; s < stage_list.size(); ++s) {
        Ref<Resource> stage = stage_list[s]; String stage_name = stage->get("stage_name");
        if (stage_name.begins_with("SimulationStage:")) continue;
        if (native && (particle_stage(stage_name) || stage_name.begins_with("EventHandler:"))) continue;
        Array ops = operations(stage);
        for (int o = 0; o < ops.size(); ++o) {
            Ref<Resource> op = ops[o];
            if (!bool(op->get("enabled"))) continue;
            if (particle_stage(stage_name) && String(op->get("module_name")) == nbfluid::source_module) {
                for (const auto &field : nbfluid::source_fields) {
                    if (declarations.has(field.name)) {
                        if (!NBNativeAttributeValues::compatible(declarations[field.name], field.type))
                            issue(layout, op, String("Conflicting fluid source attribute: ") + field.name);
                        continue;
                    }
                    declarations[field.name] = String(field.type);
                    Dictionary row;
                    row["name"] = String(field.name);
                    row["type"] = String(field.type);
                    if (String(field.name) == "Particles.fluids_source_color") row["spawn_default"] = Color(1,1,1,1);
                    row["slot"] = particles.size();
                    particles.append(row);
                }
            }
            if (String(op->get("operation_kind")) != "assignment") continue;
            String name = op->get("module_name"), target = op->get("target_type");
            if (stage_name == "EmitterSpawn" && (nbgrid::declaration(op) || nbgrid::reader_declaration(op))) continue;
            String type = NBNativeAttributeValues::type_name(target);
            if (type.is_empty()) { issue(layout, op, "Unsupported SetVariables type: " + target); continue; }
            bool particle = name.begins_with("Particles.");
            bool scope = (particle && particle_stage(stage_name)) || (name.begins_with("Emitter.") && (stage_name == "EmitterSpawn" || stage_name == "EmitterUpdate"));
            if (!scope || name.begins_with("Particles.Initial.") || name.begins_with("Particles.Previous.") ||
                named(name, {"Particles.UniqueID", "Emitter.Age", "Emitter.LoopedAge", "Emitter.NormalizedLoopAge", "Emitter.ExecutionState", "Emitter.ExecutionStateSource", "Emitter.LoopCount", "Emitter.CurrentLoopDelay", "Emitter.CurrentLoopDuration"})) {
                issue(layout, op, "SetVariables target/stage requires unsupported execution semantics: " + name); continue;
            }
            Array inputs = op->get("inputs");
            if (inputs.size() != 1) { issue(layout, op, "A typed assignment must contain exactly one value."); continue; }
            if (builtins.has(name) && !NBNativeAttributeValues::compatible(type, Array(builtins[name])[0])) {
                issue(layout, op, "Assignment type differs from the built-in attribute: " + name); continue;
            }
            if (declarations.has(name)) {
                if (!NBNativeAttributeValues::compatible(type, declarations[name])) issue(layout, op, "Conflicting typed declarations: " + name);
                continue;
            }
            declarations[name] = type;
            if (!builtins.has(name)) {
                Array rows = particle ? particles : emitter_rows;
                Dictionary row; row["name"] = name; row["type"] = type; row["slot"] = rows.size(); rows.append(row);
            }
        }
    }
    Dictionary emitter_available, at_spawn, spawn_available; bool saw_spawn = false;
    for (const char *stage_key : {"EmitterSpawn", "EmitterUpdate", "ParticleSpawn", "ParticleUpdate"}) {
        String stage_name = stage_key;
        if (native && particle_stage(stage_name)) continue;
        Ref<Resource> stage = emitter->call("get_stage", stage_name);
        if (stage.is_null()) continue;
        Dictionary available = stage_name.begins_with("Emitter") ? emitter_available.duplicate() : spawn_available.duplicate();
        Dictionary available_outputs = Dictionary(source_defaults.get(stage_name, Dictionary())).duplicate();
        if (stage_name.begins_with("Particle")) available.merge(stage_name == "ParticleSpawn" && saw_spawn ? at_spawn : emitter_available, true);
        Array ops = operations(stage);
        for (int o = 0; o < ops.size(); ++o) {
            Ref<Resource> op = ops[o];
            if (!bool(op->get("enabled"))) continue;
            String kind = op->get("operation_kind"), name = op->get("module_name");
            if (stage_name == "EmitterUpdate" && kind == "assignment" && saw_spawn)
                issue(layout, op, "Emitter assignments after spawning require per-spawn parameter snapshots that are not implemented.");
            if (stage_name == "EmitterUpdate" && named(name, {"SpawnRate", "SpawnBurst_Instantaneous", "SpawnBurst_Instantaneous@1.0"})) {
                if (!saw_spawn) { at_spawn = available.duplicate(); saw_spawn = true; }
                else { Array keys = at_spawn.keys(); for (int k = 0; k < keys.size(); ++k) if (!available.has(keys[k])) at_spawn.erase(keys[k]); }
            }
            Array inputs = op->get("inputs");
            for (int i = 0; i < inputs.size(); ++i) {
                Ref<Resource> input = inputs[i]; TypedArray<String> refs = references(input->get("value"));
                for (int r = 0; r < refs.size(); ++r) {
                    String ref = refs[r];
                    if (ref.begins_with("Output.") && !available_outputs.has(ref)) issue(layout, op, "Module output read precedes its write in this stage: " + ref);
                    String base = ref.begins_with("Particles.Initial.") ? ref.replace("Particles.Initial.", "Particles.") : ref;
                    if (declarations.has(base) && !builtins.has(base) && !available.has(ref)) issue(layout, op, "Typed attribute read precedes a supported write: " + ref);
                    if (stage_name.begins_with("Emitter") && ref.begins_with("Particles.")) issue(layout, op, "Emitter assignment cannot read per-particle state: " + ref);
                }
            }
            if (kind == "assignment" && declarations.has(name)) available[name] = true;
            if (name == nbfluid::source_module)
                for (const auto &field : nbfluid::source_fields) available[field.name] = true;
            Dictionary written = module_outputs(op, schemas); available_outputs.merge(written, true); outputs.merge(written, true);
        }
        if (stage_name.begins_with("Emitter")) emitter_available = available;
        else if (stage_name == "ParticleSpawn") {
            spawn_available = available;
            for (int p = 0; p < particles.size(); ++p) {
                Dictionary row = particles[p]; String name = row["name"];
                if (available.has(name)) { String initial_name = name.replace("Particles.", "Particles.Initial."); initial[initial_name] = name; spawn_available[initial_name] = true; }
            }
        }
    }
    TypedArray<String> requested;
    for (int s = 0; s < stage_list.size(); ++s) {
        Ref<Resource> stage = stage_list[s]; if (native && particle_stage(stage->get("stage_name"))) continue;
        Array ops = operations(stage);
        for (int o = 0; o < ops.size(); ++o) {
            Ref<Resource> op = ops[o]; if (!bool(op->get("enabled"))) continue;
            Array inputs = op->get("inputs");
            for (int i = 0; i < inputs.size(); ++i) {
                Ref<Resource> input = inputs[i]; TypedArray<String> refs = references(input->get("value"));
                for (int r = 0; r < refs.size(); ++r) if (initial.has(refs[r]) && !requested.has(refs[r])) requested.append(refs[r]);
            }
        }
    }
    Array keys = initial.keys();
    for (int i = 0; i < keys.size(); ++i) {
        String name = keys[i];
        if (!requested.has(name)) initial.erase(name);
        else { Dictionary row; row["name"] = name; row["type"] = declarations[initial[name]]; row["slot"] = particles.size(); particles.append(row); }
    }
    return layout;
}
} // namespace godot
