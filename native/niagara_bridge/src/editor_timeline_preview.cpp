#include "editor_operations.h"
#include "effect_compiler.h"
#include "value_evaluator.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <cmath>
namespace godot {
using nbdocument::dict;
TypedArray<Dictionary> NBNativeEditorOperations::timeline(const Ref<Resource> &effect,
                                                          const Dictionary &dependencies,
                                                          const Dictionary &contracts) {
    TypedArray<Dictionary> rows;
    if (effect.is_null())
        return rows;
    Array arguments;
    arguments.append(effect);
    arguments.append(Dictionary());
    arguments.append(Dictionary());
    Dictionary compiled =
        NBNativeEffectCompiler::invoke("compile_effect", arguments, dependencies, contracts);
    Array plans = compiled["emitters"];
    for (int64_t i = 0; i < plans.size(); ++i) {
        Dictionary plan = plans[i];
        Ref<Resource> emitter = plan["source"], state;
        Array stages = emitter->get("stages");
        for (int64_t s = 0; s < stages.size(); ++s) {
            Ref<Resource> stage = stages[s];
            if (stage.is_null() || stage->get("stage_name") != Variant("EmitterUpdate"))
                continue;
            Array operations = stage->get("operations");
            for (int64_t m = 0; m < operations.size(); ++m) {
                Ref<Resource> module = operations[m];
                if (module.is_valid() && bool(module->get("enabled")) &&
                    module->get("module_name") == Variant("EmitterState"))
                    state = module;
            }
        }
        String caption = state.is_null() ? "Infinite " + String::chr(0xb7) + " default"
                                         : String(bool(plan["loop"]) ? "Infinite" : "Once");
        Dictionary row = dict({{"emitter", emitter},
                               {"state", state.is_valid() ? Variant(state) : Variant()},
                               {"duration_input", Variant()},
                               {"duration", plan["duration"]},
                               {"loop", plan["loop"]},
                               {"enabled", emitter->get("enabled")},
                               {"runnable", plan["runnable"]},
                               {"known", true},
                               {"editable", state.is_valid()},
                               {"caption", caption}});
        if (state.is_valid()) {
            Array inputs = state->get("inputs");
            Variant duration = NBNativeValues::input_value(inputs, "LoopDuration", 1.0);
            String mode =
                NBNativeValues::enum_name(NBNativeValues::input_value(inputs, "LoopBehavior", "Infinite"));
            bool delayed = NBNativeValues::input_value(inputs, "UseLoopDelay", false),
                 numeric = nbdocument::number(duration) && std::isfinite(double(duration));
            row["known"] =
                numeric && double(duration) > 0 && (mode == "Once" || mode == "Infinite") && !delayed;
            row["editable"] = numeric;
            caption = mode + (delayed ? " " + String::chr(0xb7) + " delay unsupported" : String());
            if (!bool(row["known"]))
                caption = "Unresolved lifecycle " + String::chr(0xb7) + " " + caption;
            row["caption"] = caption;
            for (int64_t n = 0; n < inputs.size(); ++n) {
                Ref<Resource> entry = inputs[n];
                if (entry.is_valid() && entry->get("input_name") == Variant("LoopDuration")) {
                    row["duration_input"] = entry;
                    break;
                }
            }
        }
        rows.append(row);
    }
    return rows;
}
Dictionary NBNativeEditorOperations::snapshot_bounds(const Array &snapshot) {
    AABB bounds;
    bool populated = false;
    for (int64_t i = 0; i < snapshot.size(); ++i) {
        Dictionary particle = snapshot[i];
        Variant world = particle.get("world_bounds", Variant());
        if (world.get_type() == Variant::AABB) {
            AABB mesh = world;
            if (mesh.position.is_finite() && mesh.get_end().is_finite()) {
                bounds = populated ? bounds.merge(mesh) : mesh;
                populated = true;
                continue;
            }
        }
        Vector3 position = particle.get("world_position", Vector3());
        Vector2 size = particle.get("size_cm", Vector2());
        if (!position.is_finite() || !size.is_finite())
            continue;
        double radius = MAX(double(size.length()) * .005, .002);
        Vector3 extent = Vector3(1, 1, 1) * radius;
        AABB box(position - extent, extent * 2.0);
        bounds = populated ? bounds.merge(box) : box;
        populated = true;
    }
    return dict({{"valid", populated}, {"bounds", bounds}});
}
Vector3 NBNativeEditorOperations::frame(Camera3D *camera, const AABB &bounds, const Vector2 &viewport_size) {
    Vector3 target = bounds.get_center();
    double aspect = MAX(double(viewport_size.x) / MAX(double(viewport_size.y), 1.0), .01),
           tangent = std::tan(UtilityFunctions::deg_to_rad(camera->get_fov()) * .5);
    bool keep_height = camera->get_keep_aspect_mode() == Camera3D::KEEP_HEIGHT;
    double horizontal = keep_height ? tangent * aspect : tangent,
           vertical = keep_height ? tangent : tangent / aspect;
    Basis basis = camera->get_global_basis().orthonormalized();
    double distance = .05;
    for (int index = 0; index < 8; ++index) {
        Vector3 corner = bounds.get_endpoint(index) - target;
        double x = corner.dot(basis.get_column(0)), y = corner.dot(basis.get_column(1)),
               z = corner.dot(basis.get_column(2));
        distance = MAX(distance, z + MAX(std::abs(x) / horizontal, std::abs(y) / vertical) * 1.15);
    }
    camera->set_global_position(target + basis.get_column(2) * distance);
    camera->look_at(target, basis.get_column(1));
    camera->set_near(MIN(.05, MAX(distance - bounds.size.length() * .5, .004) * .25));
    camera->set_far(MAX(100.0, distance + bounds.size.length() * 2.0));
    return target;
}
} // namespace godot
