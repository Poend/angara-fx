#include "rng_contract.h"
#include "effect_compiler.h"
#include <godot_cpp/classes/reg_ex_match.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {
namespace {
constexpr const char *INFO = "2580a7fc4dbf69afc6eff2aaea5c79e72b70585b78d0498c3eeca76f6746155d";
constexpr const char *SCALAR = "b497fe66e2cd172129ce11beb3fed277ac5d0f4bfd59d41ef4bf37701ff5b2f0";
constexpr const char *FIXED_SEED_SCALAR = "285bb8c9697623c62efa441b4e32e75565de8ed86e271c7490bc612d4aa6a1c4";
Ref<RegEx> rx(const String &pattern) {
    return RegEx::create_from_string(pattern);
}
} // namespace
void NBNativeLegacyRNGShape::_bind_methods() {
    ClassDB::bind_static_method("NBNativeLegacyRNGShape", D_METHOD("signature", "body"),
                                &NBNativeLegacyRNGShape::signature);
    ClassDB::bind_static_method("NBNativeLegacyRNGShape",
                                D_METHOD("inspect", "name", "functions", "environment"),
                                &NBNativeLegacyRNGShape::inspect);
    ClassDB::bind_static_method("NBNativeLegacyRNGShape",
                                D_METHOD("zero_spawn_id_on_update", "source", "functions"),
                                &NBNativeLegacyRNGShape::zero_spawn_id_on_update);
}
String NBNativeLegacyRNGShape::signature(const String &body) {
    String text = rx("//[^\\n]*")->sub(body, "", true);
    text = rx("Context\\.Map(?:Spawn|Update)?\\.")->sub(text, "Context.Map.", true);
    Dictionary locals;
    TypedArray<RegExMatch> matches =
        rx("\\b(?:float[234]?|int[234]?|bool|NiagaraRandInfo)\\s+(\\w+)")->search_all(text);
    for (int i = 0; i < matches.size(); ++i) {
        Ref<RegExMatch> match = matches[i];
        String name = match->get_string(1);
        if (!locals.has(name))
            locals[name] = "v" + String::num_int64(locals.size());
    }
    matches = rx("(?<![.\\w])[A-Za-z_]\\w*")->search_all(text);
    for (int i = matches.size() - 1; i >= 0; --i) {
        Ref<RegExMatch> match = matches[i];
        String name = match->get_string();
        if (locals.has(name))
            text = text.left(match->get_start()) + String(locals[name]) + text.substr(match->get_end());
    }
    text = rx("GetRandomInfo_[A-Z0-9]+_Func")->sub(text, "GetRandomInfo_Func", true);
    return rx("\\s+")->sub(text, "", true).sha256_text();
}
Dictionary NBNativeLegacyRNGShape::inspect(const String &name, const Dictionary &functions,
                                           const Dictionary &env) {
    if (!name.begins_with("RandomRangeFloat_") && !name.begins_with("RandomRange_"))
        return Dictionary();
    Dictionary definition = functions[name];
    String shape = signature(definition["body"]), type;
    if (shape == SCALAR || shape == "f6903ffde5d0253be400cadcec752945cf532356112f8986065dfeb5955f4a68" ||
        shape == "7a151da5ff0085b87ccae271baaae14a7aa973d8dc8500f6ce74f8acfec7d4ce" ||
        shape == FIXED_SEED_SCALAR)
        type = "float";
    else if (shape == "e20fdef635a8b3e059d248bd899962e53351f900dd4ccb6bd60b552bf896ead6" ||
             shape == "51aa85abbdf29f6b5600fdad62e1d70bdc54620c2ff92c6086bcc6b09ff26b7b")
        type = "float2";
    else if (shape == "74e5eede5c95207590fb67a2581111561bd3bba56719542f87d461da2cf482a7" ||
             shape == "b9abbfa0f3bf327e3e27ff1dd36c8718f9940dd1fcda9c9fff37fa82abdccf06")
        type = "float3";
    else if (shape == "0b97e267f5f1b0c33d1cef0c5c1b16fb74bd67a01d12f1f9355b2f99a0f93b68")
        type = "float4";
    if (type.is_empty())
        return Dictionary();
    Ref<RegExMatch> helper = rx("\\b(GetRandomInfo_[A-Z0-9]+_Func)\\(")->search(definition["body"]);
    if (helper.is_null() || !functions.has(helper->get_string(1)))
        return Dictionary();
    Dictionary info = functions[helper->get_string(1)];
    String info_shape = signature(info["body"]);
    bool default_unseeded = info_shape == "7a19403f4655778fcfe5e6ad411eb6e0e4910d820eaed477470b7b3f29d1b502";
    if (info_shape != INFO && !default_unseeded)
        return Dictionary();
    auto argument = [&](const String &pin) {
        return Dictionary(env.get(pin, Dictionary())).get("value", "").stringify();
    };
    String mode = argument("In_RandomnessMode"), override_seed = argument("In_OverrideSeed");
    if (!nbcompiler::named(mode, {"0", "1"}) || !nbcompiler::named(override_seed, {"true", "false"}))
        return Dictionary();
    // Legacy scalar overloads without this input hardcode false inside the signed helper.
    if ((type != "float" || shape == SCALAR || shape == FIXED_SEED_SCALAR) && !env.has("In_FixedOverrideSeed"))
        return Dictionary();
    String fixed = env.has("In_FixedOverrideSeed") ? argument("In_FixedOverrideSeed") : String("false");
    if (!nbcompiler::named(fixed, {"true", "false"}))
        return Dictionary();
    String seed = argument("In_Seed");
    if (override_seed == "true" && (!seed.is_valid_int() || seed.to_int() < INT32_MIN || seed.to_int() > INT32_MAX))
        return Dictionary();
    String body = definition["body"];
    if ((override_seed == "true" && !body.contains("_Func(In_Seed,")) ||
        (fixed == "true" && !body.contains(", In_FixedOverrideSeed, In_RandomnessMode,")))
        return Dictionary();
    Ref<RegExMatch> map = rx("Context\\.(Map(?:Spawn|Update)?)\\.Particles\\.UniqueID")->search(info["body"]);
    if (map.is_null())
        return Dictionary();
    Dictionary result;
    if (override_seed == "true")
        result["override_seed"] = seed.to_int();
    result["fixed"] = fixed == "true";
    result["type"] = type;
    result["primitive"] = shape == SCALAR ? "rand" : "rand_float";
    result["map"] = "Context." + map->get_string(1);
    result["range_shape"] = shape;
    result["info_shape"] = info_shape;
    result["unseeded"] = default_unseeded && Dictionary(env["In_RandomnessMode"]).get("value", "").stringify() == "0";
    return result;
}
bool NBNativeLegacyRNGShape::zero_spawn_id_on_update(const String &source, const Dictionary &functions) {
    String clean = rx("//[^\\n]*")->sub(source, "", true);
    Ref<RegExMatch> wrapper =
        rx("FSimulationContext Context = "
           "\\(FSimulationContext\\)0;\\s*BRANCH\\s*if\\s*\\(bRunUpdateLogic\\)\\s*\\{([^{}]*)\\}")
            ->search(clean);
    if (wrapper.is_null())
        return false;
    if (rx("\\s+")->sub(wrapper->get_string(1), "", true) !=
        "SetupExecIndexForGPU();InitConstants(Context);LoadUpdateVariables(Context,GLinearThreadId);"
        "ReadDataSets(Context);SimulateMapUpdate(Context);WriteDataSets(Context);")
        return false;
    if (!functions.has("InitConstants"))
        return false;
    String constants = rx("\\s+")->sub(Dictionary(functions["InitConstants"])["body"], "", true);
    for (const char *binding : {"Context.MapSpawn.Emitter.RandomSeed=Emitter_RandomSeed;",
                                "Context.MapSpawn.Engine.System.TickCount=Engine_System_TickCount;"})
        if (constants.count(binding) != 1)
            return false;
    Array pending = Array::make("InitConstants", "LoadUpdateVariables", "ReadDataSets", "SimulateMapUpdate"),
          seen;
    while (!pending.is_empty()) {
        String name = pending.pop_back();
        if (seen.has(name))
            continue;
        seen.append(name);
        if (!functions.has(name))
            return false;
        String body = rx("//[^\\n]*")->sub(Dictionary(functions[name])["body"], "", true);
        if (rx("Context(?:\\.MapSpawn(?:\\.Particles(?:\\.UniqueID)?)?)?\\s*(?:[+*/-]?=|\\+\\+|--)")
                ->search(body)
                .is_valid())
            return false;
        TypedArray<RegExMatch> matches = rx("\\b([A-Za-z_]\\w*)\\(([^;{}]*)\\);")->search_all(body);
        for (int i = 0; i < matches.size(); ++i) {
            Ref<RegExMatch> call = matches[i];
            if (!call->get_string(2).contains("Context"))
                continue;
            String target = call->get_string(1);
            if (nbcompiler::named(target, {"max", "min", "NiagaraAll", "Modulo"}))
                continue;
            if (!functions.has(target))
                return false;
            pending.append(target);
        }
    }
    return true;
}
} // namespace godot
