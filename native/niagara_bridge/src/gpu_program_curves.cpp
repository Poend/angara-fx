#include "gpu_program.h"
#include "effect_compiler.h"
#include "curve_evaluator.h"
#include "value_evaluator.h"

namespace godot::nbgpu {
String Builder::curve(Variant value, int components) {
    String name = "nb_curve_" + integer(helpers.size());
    if (value.get_type() == Variant::DICTIONARY &&
        nbcompiler::equal(Dictionary(value).get("kind", ""), String("curve")) && components == 1)
        value = NBNativeCurves::prepare(value, "DI<Curve>");
    if (value.get_type() == Variant::DICTIONARY &&
        nbcompiler::equal(Dictionary(value).get("kind", ""), String("reference"))) {
        String key = Dictionary(value).get("name", "");
        value = user_parameters.get(key, Dictionary());
        static_parameters[key] = int64_t(value.hash());
    }
    if (value.get_type() == Variant::DICTIONARY &&
        nbcompiler::named(Dictionary(value).get("kind", ""),
                          {"curve_di", "color_curve_di", "vector2_curve_di", "vector3_curve_di"})) {
        Dictionary d = value;
        String type = components == 1 ? String("float") : "vec" + integer(components);
        if (!Array(d.get("errors", Array())).is_empty() || !bool(d.get("uses_lut", false))) {
            errors.append("GPU curve data interfaces require a valid bUseLUT=true curve, matching Niagara's "
                          "GPU restriction.");
            helpers.append(cat({type, " ", name, "(float time) {return ", type, "(0.0);}"}));
            return name;
        }
        PackedFloat32Array lut = d["lut"];
        int channels = components;
        if (Array(d.get("channels", Array())).size() != channels || lut.size() < channels ||
            lut.size() % channels != 0) {
            errors.append("GPU curve channel count or LUT length differs from the typed sample.");
            helpers.append(cat({type, " ", name, "(float time) {return ", type, "(0.0);}"}));
            return name;
        }
        int count = lut.size() / channels;
        Array entries;
        for (int i = 0; i < count; ++i) {
            Array parts;
            for (int c = 0; c < channels; ++c)
                parts.append(number(double(lut[i * channels + c])));
            entries.append(cat({type, "(", joined(parts, ","), ")"}));
        }
        helpers.append(cat({type,
                            " ",
                            name,
                            "(float time) { const ",
                            type,
                            " table[",
                            integer(count),
                            "]=",
                            type,
                            "[](",
                            joined(entries, ","),
                            "); float x=clamp((time-(",
                            number(d["min_time"]),
                            "))*",
                            number(d["inv_range"]),
                            "*",
                            number(count - 1),
                            ",0.0,",
                            number(count - 1),
                            "); int a=int(x); int b=min(a+1,",
                            integer(count - 1),
                            "); return mix(table[a],table[b],x-float(a)); }"}));
        return name;
    }
    if (components != 1) {
        errors.append("GPU vector/color curve sampling requires the corresponding typed curve DI.");
        helpers.append(cat({"vec", integer(components), " ", name, "(float time) {return vec",
                            integer(components), "(0.0);}"}));
        return name;
    }
    Array keys =
        value.get_type() == Variant::DICTIONARY ? Array(Dictionary(value).get("keys", Array())) : Array();
    String body = cat({"float ", name, "(float time) {\n"});
    if (keys.is_empty())
        body += "return 0.0;";
    else {
        Dictionary first = keys[0];
        body += cat({"if(time<=", number(first.get("time", 0.0)), ") return ",
                     number(first.get("value", 0.0)), ";\n"});
        for (int i = 1; i < keys.size(); ++i) {
            Dictionary left = keys[i - 1], right = keys[i];
            double duration = double(right.get("time", 0.0)) - double(left.get("time", 0.0));
            body += cat({"if(time<=", number(right.get("time", 0.0)), ") {\n"});
            if (duration <= 0.0)
                body += cat({"return ", number(left.get("value", 0.0)), ";\n"});
            else {
                body += cat({"float t=clamp((time-(", number(left.get("time", 0.0)), "))/", number(duration),
                             ",0.0,1.0);\n"});
                String interpolation = NBNativeValues::enum_name(left.get("interp", "Linear")).to_lower();
                if (interpolation == "constant")
                    body +=
                        cat({"return time<", number(right.get("time", 0.0)), "?",
                             number(left.get("value", 0.0)), ":", number(right.get("value", 0.0)), ";\n"});
                else if (interpolation == "cubic")
                    body += cat({"return (2.0*t*t*t-3.0*t*t+1.0)*", number(left.get("value", 0.0)),
                                 "+(t*t*t-2.0*t*t+t)*", number(double(left.get("leave", 0.0)) * duration),
                                 "+(-2.0*t*t*t+3.0*t*t)*", number(right.get("value", 0.0)), "+(t*t*t-t*t)*",
                                 number(double(right.get("arrive", 0.0)) * duration), ";\n"});
                else
                    body += cat({"return mix(", number(left.get("value", 0.0)), ",",
                                 number(right.get("value", 0.0)), ",t);\n"});
            }
            body += "}\n";
        }
        body += cat({"return ", number(Dictionary(keys[keys.size() - 1]).get("value", 0.0)), ";"});
    }
    helpers.append(body + "\n}");
    return name;
}
} // namespace godot::nbgpu
