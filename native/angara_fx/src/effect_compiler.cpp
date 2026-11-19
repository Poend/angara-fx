#include "effect_compiler.h"
#include "fluid_source_attributes.h"
#include "grid_stages.h"
#include "compiler_frontend.h"
#include "attribute_values.h"
#include "runtime_control.h"
#include "vm_datasets.h"
#include "value_evaluator.h"
#include "renderer_geometry.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

namespace godot {
void NBNativeEffectCompiler::_bind_methods() {
    ClassDB::bind_static_method("NBNativeEffectCompiler",
                                D_METHOD("invoke", "method", "arguments", "dependencies", "contracts"),
                                &NBNativeEffectCompiler::invoke);
}
Variant NBNativeEffectCompiler::invoke(const String &method, const Array &a, const Dictionary &dependencies,
                                       const Dictionary &contracts) {
    nbcompiler::Compiler c(dependencies, contracts);
    if (method == "effective_parameters")
        return c.parameters(a[0], a[1]);
    if (method == "compile_effect")
        return c.compile(a[0], a[1], a[2]);
    if (method == "resolved_sprite_modes")
        return NBNativeSpriteFacing::configuration(a[0], a[1]);
    if (method == "renderer_dynamic_parameter_mask")
        return c.dynamic_mask(a[0], a[1]);
    if (method == "_rate_value_supported")
        return c.rate_supported(a[0], a[1]);
    if (method == "_legacy_orientation_errors")
        return c.orientation_errors(a[0], a[1]);
    if (method == "_torus_errors")
        return c.torus_errors(a[0], a[1]);
    if (method == "_shape_errors")
        return c.shape_errors(a[0], a[1]);
    if (method == "_compile_mesh_renderer")
        return c.mesh_renderer(a[0], a[1], a[2], a[3]);
    if (method == "_check_value")
        return c.check_value(a[0], a[1]);
    if (method == "_math_arguments_valid")
        return c.math_arguments(a[0], a[1]);
    if (method == "_execution_index_arguments_valid")
        return c.index_arguments(a[0], a[1]);
    if (method == "_uses_execution_index")
        return c.uses_index(a[0]);
    if (method == "_lifecycle_expression_error")
        return c.lifecycle_expression(a[0], a[1]);
    if (method == "_lifecycle_errors")
        return c.lifecycle_errors(a[0], a[1], a[2], a[3]);
    if (method == "_curl_errors")
        return c.curl_errors(a[0], a[1]);
    if (method == "_attribute_types")
        return c.attribute_types(a[0], a[1]);
    if (method == "renderer_camera_offset_reference")
        return c.camera_offset(a[0]);
    if (method == "_camera_offset_binding_errors")
        return c.camera_errors(a[0], a[1], a[2], a[3]);
    if (method == "_written_renderer_attribute_errors")
        return c.written_errors(a[0], a[1]);
    if (method == "_curve_errors")
        return c.curve_errors(a[0], a[1]);
    if (method == "_missing_user_parameters")
        return c.missing_parameters(a[0], a[1]);
    if (method == "_source_spawn_order")
        return c.spawn_order(a[0]);
    if (method == "_mesh_scale_default")
        return c.mesh_scale(a[0]);
    if (method == "_issue") {
        c.issue(a[0], a[1], a[2], a[3], a[4]);
        return Variant();
    }
    ERR_FAIL_V_MSG(Variant(), "Unknown native Niagara compiler method: " + method);
}
} // namespace godot

namespace godot::nbcompiler {
Dictionary Compiler::compile(const Ref<Resource> &effect, Dictionary p, Dictionary options) const {
    Dictionary result;
    Array emitters, diagnostics, unresolved;
    result["emitters"] = emitters;
    result["diagnostics"] = diagnostics;
    result["fully_supported"] = true;
    result["unresolved_gpu_emitters"] = unresolved;
    if (effect.is_null())
        return result;
    options = options.duplicate();
    Dictionary execution = effect->get("source_execution");
    if (!execution.is_empty())
        options["use_compiled_source_rng"] = true;
    bool source_rng = bool(options.get("use_compiled_source_rng", false));
    Dictionary clock = NBNativeSourceClock::configuration(effect, source_rng);
    result["source_clock"] = clock;
    Array clock_errors = clock["errors"];
    for (int i = 0; i < clock_errors.size(); ++i)
        issue(result, Ref<Resource>(), Ref<Resource>(), "error", clock_errors[i]);
    p = parameters(effect, p);
    Array original_diagnostics = effect->get("diagnostics");
    for (int i = 0; i < original_diagnostics.size(); ++i) {
        Dictionary d = original_diagnostics[i];
        diagnostics.append(d.duplicate(true));
        if (named(d.get("severity", ""), {"error", "warning"}))
            result["fully_supported"] = false;
    }
    Dictionary system_lifecycle, native_system;
    result["system_lifecycle"] = system_lifecycle;
    result["native_system"] = native_system;
    bool system_errors = false;
    if (execution.has("system_programs")) {
        Ref<NBNativeVMSystem> native;
        native.instantiate();
        TypedArray<String> errors = native->configure(execution["system_programs"], p);
        for (int i = 0; i < errors.size(); ++i)
            issue(result, Ref<Resource>(), Ref<Resource>(), "error", errors[i]);
        system_errors = !errors.is_empty();
        if (!system_errors) {
            native_system = execution["system_programs"];
            result["native_system"] = native_system;
        }
        if (!bool(execution.get("source_system_deterministic", false)) &&
            int64_t(execution.get("source_platform_rand_max", 0)) < 1) {
            issue(result, Ref<Resource>(), Ref<Resource>(), "error",
                  "Native nondeterministic system requires its exported platform RAND_MAX.");
            system_errors = true;
        }
        if (!native_system.is_empty())
            issue(result, Ref<Resource>(), Ref<Resource>(), "warning",
                  "Native System/Emitter programs use explicit player LOD distance and spawn-count scale "
                  "inputs; automatic scene scalability policy remains unimplemented.");
    }
    Array system_stages = effect->get("stages");
    for (int s = 0; s < system_stages.size(); ++s) {
        if (!native_system.is_empty())
            continue;
        Ref<Resource> stage = system_stages[s];
        Array ops = stage->get("operations");
        for (int o = 0; o < ops.size(); ++o) {
            Ref<Resource> op = ops[o];
            if (!bool(op->get("enabled")))
                continue;
            if (!equal(op->get("module_name"), String("SystemState")) ||
                !equal(stage->get("stage_name"), String("SystemUpdate"))) {
                issue(result, Ref<Resource>(), op, "error",
                      "System operation is preserved but not implemented.");
                system_errors = true;
            } else {
                if (!system_lifecycle.is_empty()) {
                    issue(result, Ref<Resource>(), op, "error",
                          "Multiple SystemState modules are not implemented.");
                    system_errors = true;
                }
                system_lifecycle = NBNativeLifecycle::configuration(op, true);
                result["system_lifecycle"] = system_lifecycle;
                Dictionary empty_layout;
                empty_layout["declarations"] = Dictionary();
                empty_layout["particle"] = Array();
                TypedArray<String> errors =
                    lifecycle_errors(op, p, attribute_types(effect, empty_layout), true);
                for (int i = 0; i < errors.size(); ++i) {
                    issue(result, Ref<Resource>(), op, "error", errors[i]);
                    system_errors = true;
                }
            }
        }
    }
    Array sources = effect->get("emitters");
    const bool enabled_only = options.get("enabled_emitters_only", false);
    for (int e = 0; e < sources.size(); ++e) {
        Ref<Resource> emitter = sources[e];
        if (enabled_only && !bool(emitter->get("enabled")))
            continue;
        Array settings = emitter->get("settings");
        Variant local = NBNativeValues::input_value(settings, "LocalSpace",
                                                    NBNativeValues::input_value(settings, "bLocalSpace"));
        Dictionary plan, plan_stages, bindings;
        Array renderers;
        plan["source"] = emitter;
        plan["stages"] = plan_stages;
        plan["renderers"] = renderers;
        plan["mesh_bindings"] = bindings;
        plan["runnable"] = emitter->get("enabled");
        plan["duration"] = 1.0;
        plan["loop"] = true;
        plan["local_space"] = local.get_type() != Variant::NIL && bool(local);
        plan["interpolated_spawn"] = NBNativeValues::enum_name(NBNativeValues::input_value(
                                         settings, "InterpolatedSpawnMode", "None")) == "Interpolation";
        plan["native_system"] = !native_system.is_empty();
        Dictionary lineage = emitter->get("source_lineage");
        if (!native_system.is_empty()) {
            Dictionary c;
            c["mode"] = "Native";
            c["response"] = "Native";
            plan["lifecycle"] = c;
            if (!equal(lineage.get("verified", Variant()), true) ||
                !equal(lineage.get("compiled_spawn_order_available", Variant()), true) ||
                lineage.get("compiled_spawn_attributes", Variant()).get_type() != Variant::ARRAY) {
                issue(result, emitter, Ref<Resource>(), "error",
                      "Native system scheduling requires verified compiled SpawnInfo order.");
                plan["runnable"] = false;
            }
        }
        if (!clock_errors.is_empty() || system_errors)
            plan["runnable"] = false;
        String spawn_mode = NBNativeValues::enum_name(NBNativeValues::input_value(
            settings, "InterpolatedSpawnMode",
            NBNativeValues::input_value(settings, "InterpolatedSpawning", "RunUpdateScript")));
        plan["spawn_update_mode"] = spawn_mode;
        plan["mesh_scale_default"] = mesh_scale(emitter);
        Dictionary spawn = spawn_order(emitter);
        plan["spawn_info_order"] = spawn["order"];
        Array errors = spawn["errors"];
        for (int i = 0; i < errors.size(); ++i) {
            issue(result, emitter, Ref<Resource>(), "error", errors[i]);
            plan["runnable"] = false;
        }
        plan["interpolated_spawn"] = spawn_mode == "Interpolation";
        if (!named(spawn_mode, {"NoInterpolation", "RunUpdateScript", "Interpolation"})) {
            issue(result, emitter, Ref<Resource>(), "error",
                  "Unknown interpolated spawn mode: " + spawn_mode);
            plan["runnable"] = false;
        }
        Dictionary scoped = p.duplicate(), source_contracts = emitter->get("source_rng_contracts"),
                   source_spawn = source_contracts.get("ParticleSpawn", Dictionary());
        if (source_contracts.has("max_instance_count"))
            plan["max_instance_count"] = source_contracts["max_instance_count"];
        else {
            String mode = NBNativeValues::enum_name(NBNativeValues::input_value(settings, "AllocationMode"));
            if (mode == "Fixed" || mode == "FixedCount")
                plan["max_instance_count"] = std::max<int64_t>(0, NBNativeValues::input_value(settings, "PreAllocationCount", 0));
        }
        String backend = NBNativeValues::enum_name(NBNativeValues::input_value(settings, "SimTarget", "CPU"));
        bool native_vm =
            source_rng && !named(backend, {"GPU", "GPUComputeSim"}) && source_spawn.has("vm_program");
        plan["native_vm_requested"] = native_vm;
        Dictionary source_defaults;
        if (source_rng && named(backend, {"GPU", "GPUComputeSim"}) &&
            !bool(options.get("allow_gpu_cpu_fallback", false))) {
            for (const char *stage_name : {"ParticleSpawn", "ParticleUpdate"}) {
                Dictionary packet = source_contracts.get(stage_name, Dictionary());
                Dictionary literals = packet.get("literal_module_outputs", Dictionary());
                Ref<Resource> stage = emitter->call("get_stage", stage_name);
                if (literals.is_empty() || stage.is_null()) continue;
                Dictionary disabled_outputs;
                Array ops = stage->get("operations"), names = literals.keys();
                for (int i = 0; i < ops.size(); ++i) {
                    Ref<Resource> op = ops[i];
                    if (bool(op->get("enabled"))) continue;
                    String alias = op->get("alias_name");
                    if (alias.is_empty()) alias = String(op->get("module_name")).get_slice("@", 0).get_file();
                    for (int k = 0; k < names.size(); ++k)
                        if (String(names[k]).begins_with("Output." + alias + "."))
                            disabled_outputs[names[k]] = literals[names[k]];
                }
                if (!disabled_outputs.is_empty()) source_defaults[stage_name] = disabled_outputs;
            }
        }
        plan["source_output_defaults"] = source_defaults;
        Dictionary gpu_readers;
        if (source_rng && named(backend, {"GPU", "GPUComputeSim"}) &&
            !bool(options.get("allow_gpu_cpu_fallback", false)))
            for (const char *key : {"ParticleSpawn","ParticleUpdate"})
                gpu_readers.merge(Dictionary(source_contracts.get(key,Dictionary())).get("gpu_readers",Dictionary()));
        plan["gpu_readers"] = gpu_readers;
        Dictionary layout = NBNativeCompilerAttributes::build(emitter, native_vm, builtins,
                                                              contracts["module_outputs"], source_defaults),
                   declarations = layout["declarations"];
        Array particle_rows = layout["particle"];
        if (native_vm) {
            Array variables =
                Dictionary(Dictionary(source_spawn["vm_program"]).get("particle_dataset", Dictionary()))
                    .get("variables", Array());
            Dictionary table = NBNativeVMParticles::type_table();
            for (int i = 0; i < variables.size(); ++i) {
                Dictionary variable = variables[i];
                String actual = table.get(variable["type"], "");
                if (actual.is_empty())
                    continue;
                String name = "Particles." + String(variable["name"]);
                declarations[name] = actual;
                if (!builtins.has(name)) {
                    Dictionary row;
                    row["name"] = name;
                    row["type"] = actual;
                    row["slot"] = particle_rows.size();
                    particle_rows.append(row);
                }
            }
        }
        plan["attribute_layout"] = layout;
        Dictionary types = attribute_types(effect, layout), renderer_types = types.duplicate();
        plan["attribute_types"] = types;
        plan["renderer_parameter_types"] = renderer_types;
        Array keys = p.keys();
        for (int i = 0; i < keys.size(); ++i)
            if (!renderer_types.has(keys[i]))
                renderer_types[keys[i]] = type(p[keys[i]], Dictionary());
        errors = layout["errors"];
        for (int i = 0; i < errors.size(); ++i) {
            Dictionary d = errors[i];
            issue(result, emitter, d["operation"], "error", d["message"]);
            plan["runnable"] = false;
        }
        keys = declarations.keys();
        for (int i = 0; i < keys.size(); ++i)
            scoped[keys[i]] = NBNativeAttributeValues::zero(declarations[keys[i]]);
        for (int i = 0; i < particle_rows.size(); ++i) {
            Dictionary row = particle_rows[i];
            scoped[row["name"]] = NBNativeAttributeValues::zero(row["type"]);
        }
        Dictionary outputs = layout["outputs"];
        keys = outputs.keys();
        for (int i = 0; i < keys.size(); ++i)
            scoped[keys[i]] = NBNativeAttributeValues::zero(outputs[keys[i]]);
        Array assignments;
        plan["emitter_assignments"] = assignments;
        Ref<Resource> emitter_spawn = emitter->call("get_stage", "EmitterSpawn");
        if (emitter_spawn.is_valid()) {
            Array ops = emitter_spawn->get("operations");
            for (int i = 0; i < ops.size(); ++i) {
                Ref<Resource> op = ops[i];
                String name = op->get("module_name");
                if (bool(op->get("enabled")) && equal(op->get("operation_kind"), String("assignment")) &&
                    name.begins_with("Emitter.") && declarations.has(name))
                    assignments.append(op);
            }
        }
        for (int i = 0; i < original_diagnostics.size(); ++i) {
            Dictionary d = original_diagnostics[i];
            String name = d.get("emitter", "");
            if (bool(d.get("blocks_execution", false)) &&
                (name.is_empty() || equal(name, emitter->get("display_name"))))
                plan["runnable"] = false;
        }
        plan["source_backend"] = backend;
        plan["backend"] = "cpu";
        plan["native_backend_supported"] = !named(backend, {"GPU", "GPUComputeSim"});
        Variant parent = emitter->get("parent_emitter_source");
        if (parent.get_type() != Variant::NIL && !String(parent).is_empty()) {
            issue(result, emitter, Ref<Resource>(), "error",
                  "Inherited emitter contains overrides only; unresolved parent " + String(parent) +
                      " must be composed before execution.");
            plan["runnable"] = false;
        }
        if (NBNativeValues::enum_name(
                NBNativeValues::input_value(settings, "CalculateBoundsMode", "Dynamic")) == "Fixed")
            issue(result, emitter, Ref<Resource>(), "warning",
                  "Source fixed bounds are retained; this renderer derives dynamic bounds from particles and "
                  "does not reproduce source fixed-bounds culling.");
        if (local.get_type() == Variant::NIL)
            issue(result, emitter, Ref<Resource>(), "warning",
                  "LocalSpace metadata is absent. World space is the provisional default; provide an "
                  "explicit setting to confirm it.");
        if (bool(plan["interpolated_spawn"]))
            issue(result, emitter, Ref<Resource>(), "warning",
                  "Interpolated spawning uses event birth times and source transform interpolation; exact "
                  "Niagara interpolation compensation is not validated.");
        Array stages = emitter->get("stages");
        Dictionary grid_program;
        String grid_error = nbgrid::compile(emitter, grid_program);
        if (!grid_error.is_empty() || (!grid_program.is_empty() &&
            (!named(backend, {"GPU","GPUComputeSim"}) || bool(options.get("allow_gpu_cpu_fallback",false))))) {
            issue(result, emitter, Ref<Resource>(), "error", grid_error.is_empty() ?
                String("Grid3D simulation stages require the native GPU backend.") : grid_error);
            plan["runnable"] = false;
        }
        if (!grid_program.is_empty()) plan["grid_stages"] = grid_program;
        Dictionary module_stages = contracts["module_stages"];
        Array bool_inputs = contracts["constant_bool_inputs"];
        for (int s = 0; s < stages.size(); ++s) {
            Ref<Resource> stage = stages[s];
            String stage_name = stage->get("stage_name");
            Dictionary stage_values = scoped;
            if (source_defaults.has(stage_name)) {
                stage_values = scoped.duplicate();
                stage_values.merge(source_defaults[stage_name], true);
            }
            Array ops = stage->get("operations");
            plan_stages[stage_name] = ops;
            if (stage_name.begins_with("SimulationStage:")) continue;
            for (int o = 0; o < ops.size(); ++o) {
                Ref<Resource> op = ops[o];
                String name = op->get("module_name");
                if (!bool(op->get("enabled")))
                    continue;
                if (stage_name == "EmitterSpawn" && (nbgrid::declaration(op) || nbgrid::reader_declaration(op))) continue;
                if (!native_system.is_empty() && named(stage_name, {"EmitterSpawn", "EmitterUpdate"}))
                    continue;
                if (native_vm &&
                    (named(stage_name, {"ParticleSpawn", "ParticleUpdate"}) ||
                     (stage_name.begins_with("EventHandler:") && source_contracts.has(stage_name))))
                    continue;
                Array inputs = op->get("inputs");
                if (!named(stage_name, {"ParticleSpawn", "ParticleUpdate"}))
                    for (int i = 0; i < inputs.size(); ++i) {
                        Ref<Resource> item = inputs[i];
                        if (uses_index(item->get("value"))) {
                            issue(
                                result, emitter, op, "error",
                                "Normalized execution index requires a supported particle execution stage.");
                            plan["runnable"] = false;
                        }
                    }
                if (equal(op->get("operation_kind"), String("assignment"))) {
                    if (!declarations.has(name)) {
                        issue(result, emitter, op, "error", "Unsupported/untyped assignment target.");
                        plan["runnable"] = false;
                        continue;
                    }
                    for (int i = 0; i < inputs.size(); ++i) {
                        Ref<Resource> item = inputs[i];
                        Variant value = item->get("value");
                        errors = curve_errors(value, p);
                        for (int r = 0; r < errors.size(); ++r) {
                            issue(result, emitter, op, "error", errors[r]);
                            plan["runnable"] = false;
                        }
                        if (!check_value(value, stage_values)) {
                            issue(result, emitter, op, "error",
                                  "Unsupported or unresolved assignment input.");
                            plan["runnable"] = false;
                        }
                        String error = NBNativeCompilerAttributes::assignment_error(
                            value, NBNativeAttributeValues::type_name(op->get("target_type")), types,
                            builtins, specs);
                        if (!error.is_empty()) {
                            issue(result, emitter, op, "error", error);
                            plan["runnable"] = false;
                        }
                    }
                    continue;
                }
                const bool reader_module = gpu_readers.has(op->get("stable_id"));
                if ((!reader_module && (!module_stages.has(name) || !Array(module_stages[name]).has(stage_name))) ||
                    (name == nbfluid::source_module && !named(backend, {"GPU", "GPUComputeSim"}))) {
                    issue(result, emitter, op, "error",
                          "Module/version or stage is not implemented by the shared runtime.");
                    plan["runnable"] = false;
                    continue;
                }
                bool invalid_controls = false;
                Array checked;
                for (int i = 0; i < inputs.size(); ++i) {
                    Ref<Resource> item = inputs[i];
                    String key = item->get("input_name");
                    bool dynamic =
                        named(name, {"DynamicMaterialParameters@1.0", "DynamicMaterialParameters@1.1"}) &&
                        named(key.trim_suffix("1").trim_suffix("2").trim_suffix("3"),
                              {"Param0WriteEnabled", "Param1WriteEnabled", "Param2WriteEnabled",
                               "Param3WriteEnabled"});
                    if ((!bool_inputs.has(key) && !dynamic) || checked.has(key))
                        continue;
                    checked.append(key);
                    if (item->get("value").get_type() != Variant::BOOL) {
                        issue(result, emitter, op, "error",
                              "Control input requires an authored boolean constant: " + key);
                        invalid_controls = true;
                    }
                }
                if (invalid_controls) {
                    plan["runnable"] = false;
                    continue;
                }
                for (int i = 0; i < inputs.size(); ++i) {
                    Ref<Resource> item = inputs[i];
                    String key = item->get("input_name");
                    Variant value = item->get("value");
                    if (!(name == "BoxLocation" && key == "RandomnessMode") && key.ends_with("RandomnessMode") &&
                        NBNativeValues::enum_name(value) != "SimulationDefaults") {
                        issue(result, emitter, op, "error",
                              "Randomness override mode is not implemented: " + key);
                        plan["runnable"] = false;
                    }
                    errors = missing_parameters(value, p);
                    for (int r = 0; r < errors.size(); ++r) {
                        issue(result, emitter, op, "error",
                              "Missing per-instance parameter " + String(errors[r]) + " for input " + key);
                        plan["runnable"] = false;
                    }
                    bool unit_sphere = false;
                    if (reader_module && value.get_type() == Variant::DICTIONARY) {
                        Dictionary call = value;
                        if (call.get("name","") == Variant("V2/RandomVector")) {
                            Array args = call.get("arguments",Array());
                            unit_sphere = !bool(NBNativeValues::input_value(args,"ConstraintoHemisphere",false)) &&
                                check_value(NBNativeValues::input_value(args,"VectorScale",1.),stage_values);
                        }
                    }
                    if (!unit_sphere && !check_value(value, stage_values)) {
                        issue(result, emitter, op, "error", "Unsupported dynamic input: " + key);
                        plan["runnable"] = false;
                    }
                    errors = curve_errors(value, p);
                    for (int r = 0; r < errors.size(); ++r) {
                        issue(result, emitter, op, "error", errors[r]);
                        plan["runnable"] = false;
                    }
                    if (value.get_type() == Variant::DICTIONARY) {
                        Dictionary d = value;
                        if (equal(d.get("kind", ""), String("reference")) &&
                            String(d.get("name", "")).begins_with("User.") &&
                            (key.ends_with("Mode") || key.ends_with("CoordinateSpace") ||
                             key.begins_with("Use") || key.begins_with("Write") ||
                             key.begins_with("Adjust"))) {
                            issue(result, emitter, op, "error",
                                  "Mode switches require authored constants: " + key);
                            plan["runnable"] = false;
                        }
                    }
                }
                validate_operation(emitter, op, plan, result, p, stage_values, options);
            }
        }
        Array renderer_sources = emitter->get("renderers");
        for (int r = 0; r < renderer_sources.size(); ++r) {
            Ref<Resource> source = renderer_sources[r];
            if (bool(source->get("enabled")))
                renderer(effect, emitter, source, plan, result, p, backend);
        }
        if (named(backend, {"GPU", "GPUComputeSim"})) {
            if (source_rng) {
                plan["backend"] = "gpu_compute";
                plan["source_rng"] = external("source_rng", "build", Array::make(effect, plan, p));
            }
            Dictionary gpu = external("gpu", "build", Array::make(plan, scoped));
            if (!grid_program.is_empty()) gpu["grid_stages"] = grid_program;
            plan["gpu_program"] = gpu;
            plan["native_backend_supported"] = bool(gpu["supported"]) && bool(plan["runnable"]);
            plan["gpu_device_available"] = bool(options.get(
                "native_gpu_available", RenderingServer::get_singleton()->get_rendering_device() != nullptr));
            if (bool(options.get("allow_gpu_cpu_fallback", false))) {
                plan["backend"] = "cpu_fallback";
                unresolved.append(emitter->get("stable_id"));
                issue(result, emitter, Ref<Resource>(), "warning",
                      "GPU emitter explicitly uses the CPU comparison fallback. Source GPU requirements "
                      "remain unresolved in this mode.");
            } else {
                plan["backend"] = "gpu_compute";
                errors = gpu["errors"];
                for (int i = 0; i < errors.size(); ++i) {
                    issue(result, emitter, Ref<Resource>(), "error", errors[i]);
                    plan["runnable"] = false;
                }
                if (!bool(plan["gpu_device_available"])) {
                    issue(result, emitter, Ref<Resource>(), "error",
                          "Native GPU particles require Forward+ or Mobile RenderingDevice; Compatibility "
                          "and headless cannot execute compute shaders.");
                    plan["runnable"] = false;
                }
                if (!bool(plan["runnable"]))
                    unresolved.append(emitter->get("stable_id"));
                else if (!source_rng)
                    issue(result, emitter, Ref<Resource>(), "warning",
                          "Native GPU modules use an independent deterministic RNG and synchronous snapshot "
                          "readback. Niagara RNG/order and interpolated GPU first-frame behavior remain "
                          "unverified; this backend has no performance-equivalence claim.");
                else
                    issue(result, emitter, Ref<Resource>(), "warning",
                          "Native GPU comparison uses synchronous snapshot readback; it has no "
                          "performance-equivalence claim.");
                if (bool(plan["runnable"]) && plan.has("lifecycle")) {
                    Dictionary lifecycle = plan["lifecycle"];
                    if (equal(lifecycle["mode"], String("System")) ||
                        equal(lifecycle["response"], String("Complete")))
                        issue(result, emitter, Ref<Resource>(), "warning",
                              "GPU completion uses synchronously observed particle counts. Niagara's fenced "
                              "asynchronous count can delay execution-state completion; exact completion "
                              "timing remains unverified.");
                }
            }
        }
        plan["rng_backend"] = "godot_independent";
        if (source_rng) {
            plan["rng_backend"] = "ue_compiled_unsupported";
            Dictionary rng = external("source_rng", "build", Array::make(effect, plan, p));
            plan["source_rng"] = rng;
            errors = rng["errors"];
            for (int i = 0; i < errors.size(); ++i) {
                issue(result, emitter, Ref<Resource>(), "error", errors[i]);
                plan["runnable"] = false;
            }
            if (bool(rng["supported"])) {
                plan["rng_backend"] = "ue_compiled_deterministic";
                if (native_vm)
                    plan["rng_backend"] = "ue_native_vm";
                if (!native_vm &&
                    (bool(rng.get("uses_unseeded_gpu", false)) || bool(rng.get("uses_instance_seed", false))))
                    plan["rng_backend"] = "ue_compiled_source_random";
                if (equal(plan["backend"], String("gpu_compute"))) {
                    Dictionary gpu = external("gpu", "build", Array::make(plan, scoped));
                    if (!grid_program.is_empty()) gpu["grid_stages"] = grid_program;
                    plan["gpu_program"] = gpu;
                    errors = gpu["errors"];
                    for (int i = 0; i < errors.size(); ++i) {
                        issue(result, emitter, Ref<Resource>(), "error", errors[i]);
                        plan["runnable"] = false;
                    }
                    plan["native_backend_supported"] = bool(gpu["supported"]) && bool(plan["runnable"]);
                }
                issue(result, emitter, Ref<Resource>(), "warning",
                      "Compiled source RNG uses its explicit seed provenance and source SpawnInfo order. "
                      "Platform VM/GPU arithmetic and material parity remain unverified.");
            }
        }
        emitters.append(plan);
    }
    Dictionary graph = external("events", "graph", Array::make(emitters));
    result["event_execution_order"] = graph["order"];
    Array errors = graph["errors"];
    for (int i = 0; i < errors.size(); ++i)
        issue(result, Ref<Resource>(), Ref<Resource>(), "error", errors[i]);
    issue(result, Ref<Resource>(), Ref<Resource>(), "warning",
          "Bridge runtime uses the inspected UE5.8.3 solver/size branches. RNG backend is reported per "
          "emitter; source RNG requires a matched compiled contract. Unversioned source module identity and "
          "Unreal visual equivalence remain unverified.");
    return result;
}
} // namespace godot::nbcompiler
