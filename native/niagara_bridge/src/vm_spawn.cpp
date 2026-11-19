#include "vm_datasets.h"
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <algorithm>
using namespace godot;
namespace {
Variant zero(const String &type) {
    if (type == "bool")
        return false;
    if (type == "int")
        return 0;
    if (type == "float")
        return 0.;
    if (type == "vec2")
        return Vector2();
    if (type == "vec3" || type == "position")
        return Vector3();
    if (type == "color")
        return Color(0, 0, 0, 0);
    if (type == "quat")
        return Quaternion(0, 0, 0, 0);
    if (type == "vec4")
        return Vector4();
    return Variant();
}
} // namespace
Dictionary NBNativeVMParticles::append_spawn_templates(Dictionary emitter, int count, double time,
                                                       double delta, double start_dt, double interval,
                                                       const Transform3D &source_start,
                                                       const Transform3D &source_end) {
    Dictionary result;
    TypedArray<String> errors;
    Array created;
    result["errors"] = errors;
    result["particles"] = created;
    if (count < 0 || count > 1000000) {
        errors.append("Invalid native spawn template count.");
        return result;
    }
    Ref<RandomNumberGenerator> stream = emitter.get("rng", Variant());
    if (stream.is_null()) {
        errors.append("Native spawn templates require the emitter RNG.");
        return result;
    }
    Dictionary plan = emitter.get("plan", Dictionary()), layout = plan.get("attribute_layout", Dictionary());
    Array attributes = layout.get("particle", Array());
    bool interpolated = bool(plan.get("interpolated_spawn", false)),
         local = bool(plan.get("local_space", false));
    String mode = plan.get("spawn_update_mode", "RunUpdateScript");
    Vector3 mesh_scale = plan.get("mesh_scale_default", Vector3(1, 1, 1));
    int64_t first = emitter.get("emitted", 0);
    for (int index = 0; index < count; index++) {
        float offset = float(start_dt + float(interval * index));
        double birth = interpolated ? time + offset : time;
        if (mode == "RunUpdateScript")
            birth = time + delta - float(float(delta) - float(float(delta) - start_dt));
        float weight = delta > 0 ? float(std::clamp(double(offset) / delta, 0., 1.)) : 1.f;
        Transform3D transform = interpolated ? source_start.interpolate_with(source_end, weight) : source_end;
        Ref<RandomNumberGenerator> rng;
        rng.instantiate();
        rng->set_seed(stream->randi());
        Dictionary p;
        p["id"] = first + index;
        p["birth_time"] = birth;
        p["age"] = 0.;
        p["normalized_age"] = 0.;
        p["lifetime"] = 1.;
        p["alive"] = true;
        p["first_frame"] = true;
        p["newborn"] = true;
        p["spawn_execution_index"] = index;
        p["spawn_execution_count"] = count;
        p["update_execution_index"] = 0;
        p["update_execution_count"] = 1;
        p["position"] =
            local ? Vector3() : Vector3(-transform.origin.z, transform.origin.x, transform.origin.y) * 100.f;
        for (const char *name :
             {"velocity", "previous_velocity", "initial_position", "force", "advection_velocity",
              "presolve_position", "presolve_velocity", "presolve_force"})
            p[name] = Vector3();
        p["alignment"] = Vector3(0, 1, 0);
        p["size"] = Vector2(1, 1);
        p["initial_size"] = Vector2(1, 1);
        p["size_scale_factor"] = Vector2(1, 1);
        p["mesh_scale"] = mesh_scale;
        p["initial_mesh_scale"] = mesh_scale;
        p["mesh_scale_factor"] = Vector3(1, 1, 1);
        for (const char *name : {"mesh_orientation", "initial_mesh_orientation", "initial_rotation"})
            p[name] = Quaternion();
        p["mesh_index"] = 0;
        p["ribbon_width"] = 1.;
        p["color"] = Color(1, 1, 1, 1);
        p["initial_color"] = Color(1, 1, 1, 1);
        p["dynamic"] = Color(0, 0, 0, 0);
        p["dynamic1"] = Color(1, 1, 1, 1);
        p["dynamic2"] = Color(0, 0, 0, 0);
        p["dynamic3"] = Color(0, 0, 0, 0);
        p["mass"] = 1.;
        for (const char *name :
             {"drag", "rotation", "rotational_drag", "subimage_index", "material_random", "camera_offset"})
            p[name] = 0.;
        p["drag_ignore_mass"] = true;
        p["uv_flip"] = 1.;
        p["uv_flip_y"] = 1.;
        p["cache"] = Dictionary();
        p["rng"] = rng;
        Dictionary values;
        for (int n = 0; n < attributes.size(); n++) {
            Dictionary row = attributes[n];
            values[row.get("name", "")] = zero(row.get("type", ""));
        }
        p["attributes"] = values;
        p["vm_needs_spawn"] = true;
        created.append(p);
    }
    Array particles = emitter.get("particles", Array());
    particles.append_array(created);
    emitter["particles"] = particles;
    emitter["emitted"] = first + count;
    return result;
}
