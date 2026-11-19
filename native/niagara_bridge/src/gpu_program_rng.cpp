#include "gpu_program.h"
#include "effect_compiler.h"
#include "particle_forces.h"

namespace godot::nbgpu {
using nbcompiler::reference;
String Builder::intrinsic(const String &target, std::initializer_list<String> bounds, const String &type, int module_draw) {
    auto it = bounds.begin();
    String minimum = *it++, maximum = *it;
    if (!source_rng.is_empty())
        return native_range(cat({operation->get("stable_id"), "/$", target}), {minimum, maximum}, type);
    if (module_draw) {
        bool override_mode = flag("OverrideRandomness", false), fixed = flag("FixedRandomSeed", false);
        String selected = override_mode ? mode("RandomnessMode", "SimulationDefaults") : String("SimulationDefaults");
        if ((override_mode || fixed) && (emitter_deterministic ||
            nbcompiler::named(selected, {"Deterministic", "Determinisitic", "NewEnumerator1"}))) {
            String seed = override_mode && flag("OverrideSeed", false) ? input("RandomSeed", 0, "int") : integer(emitter_seed);
            seed = cat({"uint(", seed, ")"});
            String random = cat({type == "vec4" ? "nb_ue_rand4(" : "nb_ue_rand3(",
                fixed ? seed : String("uint(p.id)"), ",", fixed ? String("0u") : int_parameter("Engine.System.TickCount"),
                ",", fixed ? String("0u") : seed, ",", integer(module_draw), "u)", type == "float" ? ".x" : ""});
            return cat({"(", minimum, "+(", maximum, "-", minimum, ")*", random, ")"});
        }
    }
    if (type == "float")
        return cat({"nb_range(p,", minimum, ",", maximum, ")"});
    String name = "nb_range_" + integer(helpers.size()),
           body = cat({type, " lo=", minimum, "; ", type, " hi=", maximum, "; ", type, " value;"});
    for (int i = 0; i < type.right(1).to_int(); ++i) {
        String c = String("xyzw").substr(i, 1);
        body += cat({"value.", c, "=nb_range(p,lo.", c, ",hi.", c, ");"});
    }
    helpers.append(cat(
        {type, " ", name, "(inout Particle p,float dt,float time,bool spawning){", body, "return value;}"}));
    return name + "(p,dt,time,spawning)";
}
String Builder::int_parameter(const String &name) {
    int slot = -1;
    for (int i = 0; i < parameters.size(); ++i)
        if (nbcompiler::equal(Dictionary(parameters[i])["name"], name))
            slot = i;
    if (slot < 0) {
        slot = parameters.size();
        Dictionary parameter;
        parameter["name"] = name;
        parameter["type"] = "int";
        parameters.append(parameter);
    }
    return cat({"floatBitsToUint(params.data[", integer(8 + slot), "].x)"});
}
String Builder::native_range(const String &path, std::initializer_list<String> bounds, const String &type,
                             bool as_integer) {
    auto it = bounds.begin();
    String minimum = cat({"(", *it++, ")"}), maximum = cat({"(", *it, ")"});
    Dictionary bindings = Dictionary(source_rng.get("stages", Dictionary())).get(stage_name, Dictionary());
    if (!bindings.has(path)) {
        errors.append(cat({"GPU native RNG input has no compiled binding: ", path}));
        return cat({type, "(0.0)"});
    }
    Dictionary call = bindings[path];
    int components = call["components"];
    String random;
    if (nbcompiler::equal(call.get("random_mode", "seeded"), String("unseeded"))) {
        String counter = integer(uint32_t(-1 - int64_t(call["unseeded_call_index"]))) + "u";
        if (stage_name == "ParticleUpdate")
            counter = cat({"(", counter, "-(p.spawned_this_dispatch?",
                           integer(source_rng["gpu_spawn_unseeded_calls"]), "u:0u))"});
        random = cat({"nb_ue_rand4(gl_GlobalInvocationID.x,", int_parameter("_NB.GPU.DispatchCounter"),
                      ",gl_GlobalInvocationID.x,", counter, ")"});
        if (nbcompiler::equal(call.get("primitive", "rand"), String("rand_float")) && components > 1) {
            Array pieces;
            for (int i = 0; i < components; ++i)
                pieces.append(
                    cat({"nb_ue_rand4(gl_GlobalInvocationID.x,", int_parameter("_NB.GPU.DispatchCounter"),
                         ",gl_GlobalInvocationID.x,(", counter, "-", integer(i), "u)).x"}));
            random = cat({"vec", integer(components), "(", joined(pieces, ","), ")"});
        }
    } else {
        String seed3 =
            integer(uint32_t(int64_t(call.get("verified_seed3_constant", source_rng["seed"])))) + "u";
        if (bool(call.get("verified_instance_seed", false)))
            seed3 = int_parameter("Engine.Emitter.InstanceSeed");
        String seed1 = call.has("verified_seed1_constant")
                           ? integer(uint32_t(int64_t(call["verified_seed1_constant"]))) + "u"
                           : String("uint(p.id)");
        if (bool(call.get("verified_seed1_emitter", false)))
            seed1 = integer(uint32_t(int64_t(source_rng["seed"]))) + "u";
        Variant id = call.get("call_id", Variant());
        int64_t seed2 = call.get("verified_seed2_constant", id.get_type() == Variant::NIL ? Variant(0) : id);
        String second = integer(uint32_t(seed2)) + "u";
        if (bool(call.get("verified_tick_seed", false)))
            second = int_parameter("Engine.System.TickCount");
        random = cat({components == 4 ? "nb_ue_rand4" : "nb_ue_rand3", "(", seed1, ",", second, ",", seed3,
                      ",", integer(call["counter"]), "u)"});
    }
    random += type == "float" || type == "int" ? ".x" : type == "vec2" ? ".xy" : type == "vec3" ? ".xyz" : "";
    if (type == "int")
        return cat({"(", minimum, "+int(float(", maximum, "-", minimum, "+1)*", random, "))"});
    if (as_integer)
        return cat({"(", minimum, "+float(int((", maximum, "-", minimum, "+1.0)*", random, ")))"});
    return cat({"(", minimum, "+(", maximum, "-", minimum, ")*", random, ")"});
}
String Builder::curl() {
    int quality = NBNativeCurlNoise::quality(operation);
    if (quality < 0)
        return unsupported("Curl Noise procedural quality is not implemented.");
    if (!NBNativeCurlNoise::valid_seed(operation->call("get_input", "RandomSeed", 11)))
        return unsupported("Curl Noise requires a constant signed32 RandomSeed.");
    String name = "nb_curl_field_" + integer(quality);
    bool exists = false;
    for (int i = 0; i < vector_fields.size(); ++i)
        if (nbcompiler::equal(Dictionary(vector_fields[i])["name"], name))
            exists = true;
    Array fields = contracts["curl_fields"];
    if (!exists) {
        if (vector_fields.is_empty())
            helpers.append(contracts["vector_sample"]);
        int binding = 3 + vector_fields.size();
        Dictionary field;
        field["name"] = name;
        field["binding"] = binding;
        field["resource"] = fields[quality];
        field["tiling"] = Vector3i(1, 1, 1);
        vector_fields.append(field);
        helpers.append(cat({"layout(set=0,binding=", integer(binding), ") uniform sampler3D ", name, ";"}));
    }
    Ref<Resource> field = fields[quality];
    String phase =
        source_rng.is_empty()
            ? literal(NBNativeCurlNoise::ordinary_phase(operation, emitter_seed), "float")
            : native_range(cat({operation->get("stable_id"), "/$CurlOffset"}), {"0.0", "4096.0"}, "float");
    String offset = cat({input("RandomizationVector", Array::make(.65, .125, .37), "vec3"), "*", phase});
    String code =
        cat({"precise vec3 curl_position=", input("SamplePosition", reference("Particles.Position"), "vec3"),
             ";\n"});
    if (nbcompiler::equal(operation->get("module_name"), String("V2/CurlNoiseForce@1.2"))) {
        code += cat({"curl_position=nb_transform_between(curl_position,2,", space("CoordinateSpace", "World"),
                     ",false,true,p,spawning);\n"});
        code += "if(params.data[7].x>0.5) "
                "curl_position=nb_transform_between(curl_position,1,2,true,true,p,spawning);\n";
    }
    if (flag("ScaleCurlLookupByEngineOwnerScale", true))
        code += "curl_position/=nb_owner_scale(p,spawning);\n";
    code += cat({"precise vec3 curl_offset=", offset,
                 "; precise float curl_frequency=", input("NoiseFrequency", 10.0),
                 "/1000.0; precise vec3 curl_point=(curl_offset+curl_position)*curl_frequency;\n"});
    if (flag("PanNoise", false))
        code += cat({"curl_point-=", input("PanNoiseField", Array::make(.2, .5, .9), "vec3"), "*",
                     expression(reference("Emitter.Age"), "float", "curl_age"), ";\n"});
    return code +
           cat({"curl_point*=125.0; p.force+=nb_sample_vector_field(", name, ",curl_point,",
                literal(Array(field->get("bounds_min")), "vec3"), ",",
                literal(Array(field->get("bounds_max")), "vec3"), ")*", input("NoiseStrength", 10.0), ";"});
}
} // namespace godot::nbgpu
