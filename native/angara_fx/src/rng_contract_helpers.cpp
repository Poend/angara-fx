#include "rng_contract.h"
#include "effect_compiler.h"
#include <godot_cpp/classes/reg_ex_match.hpp>

namespace godot {
Ref<RegEx> NBNativeRNGContract::regex(const String &pattern) {
    auto found = patterns.find(pattern);
    if (found != patterns.end())
        return found->second;
    Ref<RegEx> result = RegEx::create_from_string(pattern);
    patterns.emplace(pattern, result);
    return result;
}
void NBNativeRNGContract::error(const String &message, int64_t line_number) {
    Dictionary d;
    d["severity"] = "error";
    d["message"] = message;
    d["line"] = line_number;
    d["blocks_native_rng_parity"] = true;
    diagnostics.append(d);
}
Array NBNativeRNGContract::unite(const Array &left, const Array &right) {
    Array result = left.duplicate();
    for (int i = 0; i < right.size(); ++i)
        if (!result.has(right[i]))
            result.append(right[i]);
    return result;
}
int NBNativeRNGContract::components(const String &type) {
    String suffix = type.right(1);
    return suffix == "2" || suffix == "3" || suffix == "4" ? suffix.to_int() : 1;
}
int64_t NBNativeRNGContract::close_brace(const String &text, int64_t begin) {
    int depth = 0;
    for (int64_t i = begin; i < text.length(); ++i) {
        if (text[i] == '{')
            ++depth;
        else if (text[i] == '}' && --depth == 0)
            return i;
    }
    return -1;
}
int64_t NBNativeRNGContract::line(int64_t offset) const {
    return source.substr(0, offset).count("\n") + 1;
}
Array NBNativeRNGContract::arguments(const String &expression) {
    Array result;
    int depth = 0;
    int64_t begin = 0;
    for (int64_t i = 0; i < expression.length(); ++i) {
        char32_t c = expression[i];
        if (c == '(')
            ++depth;
        else if (c == ')')
            --depth;
        else if (c == ',' && depth == 0) {
            result.append(expression.substr(begin, i - begin).strip_edges());
            begin = i + 1;
        }
    }
    if (begin < expression.length())
        result.append(expression.substr(begin).strip_edges());
    return result;
}
Array NBNativeRNGContract::tokens(const String &expression) {
    Array result;
    TypedArray<RegExMatch> matches =
        regex("[A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)*")->search_all(expression);
    for (int i = 0; i < matches.size(); ++i) {
        Ref<RegExMatch> match = matches[i];
        result.append(match->get_string());
    }
    return result;
}
String NBNativeRNGContract::component(const String &value) {
    if (value == "x" || value == "r")
        return "x";
    if (value == "y" || value == "g")
        return "y";
    if (value == "z" || value == "b")
        return "z";
    if (value == "w" || value == "a")
        return "w";
    return "";
}
String NBNativeRNGContract::resolve(const String &expression, const Dictionary &environment) {
    if (expression.begins_with("Context."))
        return expression;
    if (environment.has(expression))
        return Dictionary(environment[expression])["value"].stringify();
    return expression;
}
String NBNativeRNGContract::constant_literal(const String &expression, const Dictionary &environment) {
    String value = expression.strip_edges();
    if (value == "true" || value == "false" || value.is_valid_int())
        return value;
    Dictionary scope = value.begins_with("Context.") ? globals : environment;
    return scope.has(value) ? Dictionary(scope[value]).get("constant", "").stringify() : String();
}
Variant NBNativeRNGContract::constant_condition(const String &text, const Dictionary &environment) {
    Ref<RegExMatch> match = regex("^if\\s*\\(([^()]*)\\)\\s*\\{?$")->search(text);
    if (match.is_null())
        return Variant();
    String value = constant_literal(match->get_string(1), environment);
    return value == "true" ? Variant(true) : value == "false" ? Variant(false) : Variant();
}
bool NBNativeRNGContract::contains_rng_call(const String &text, const Array &seen) {
    TypedArray<RegExMatch> matches = regex("([A-Za-z_][A-Za-z0-9_]*)[ \\t]*\\(")->search_all(text);
    for (int i = 0; i < matches.size(); ++i) {
        Ref<RegExMatch> match = matches[i];
        String name = match->get_string(1);
        if (nbcompiler::named(name, {"rand", "rand_float", "rand_int"}))
            return true;
        if (functions.has(name) && !seen.has(name)) {
            Array next = seen.duplicate();
            next.append(name);
            if (contains_rng_call(Dictionary(functions[name])["body"], next))
                return true;
        }
    }
    return false;
}
Array NBNativeRNGContract::tags(const String &expression, const Dictionary &environment) {
    Array result, identifiers = tokens(expression);
    for (int i = 0; i < identifiers.size(); ++i) {
        String token = identifiers[i];
        Dictionary scope = token.begins_with("Context.") ? globals : environment;
        if (token.contains(".")) {
            String root = token.left(token.rfind(".")),
                   c = component(token.get_slice(".", token.get_slice_count(".") - 1));
            Dictionary cs = Dictionary(scope.get(root, Dictionary())).get("component_tags", Dictionary());
            if (!c.is_empty() && cs.has(c)) {
                result = unite(result, cs[c]);
                continue;
            }
        }
        Dictionary value = scope.get(token, Dictionary());
        result = unite(result, value.get("tags", Array()));
        String parent = token;
        while (parent.contains(".")) {
            parent = parent.left(parent.rfind("."));
            result = unite(result, Dictionary(scope.get(parent, Dictionary())).get("tags", Array()));
        }
        Array keys = scope.keys();
        for (int k = 0; k < keys.size(); ++k)
            if (String(keys[k]).begins_with(token + "."))
                result = unite(result, Dictionary(scope[keys[k]]).get("tags", Array()));
    }
    return result;
}
Dictionary NBNativeRNGContract::component_tags(const String &expression, const Dictionary &environment) {
    if (regex("^[A-Za-z_][A-Za-z0-9_.]*$")->search(expression).is_null())
        return Dictionary();
    Dictionary scope = expression.begins_with("Context.") ? globals : environment,
               result =
                   Dictionary(
                       Dictionary(scope.get(expression, Dictionary())).get("component_tags", Dictionary()))
                       .duplicate(true);
    Array keys = scope.keys();
    for (int i = 0; i < keys.size(); ++i) {
        String key = keys[i];
        if (!key.begins_with(expression + String(".")))
            continue;
        String c = component(key.trim_prefix(expression + String(".")));
        if (!c.is_empty())
            result[c] = unite(result.get(c, Array()), Dictionary(scope[key]).get("tags", Array()));
    }
    return result;
}
void NBNativeRNGContract::store(const String &target, Dictionary value, Dictionary environment) {
    if (!target.begins_with("Context.")) {
        environment[target] = value;
        return;
    }
    globals[target] = value;
    String semantic = String(".").join(target.split(".").slice(2));
    Array dependencies = value["tags"];
    for (int i = 0; i < dependencies.size(); ++i) {
        int64_t tag = dependencies[i];
        if (tag >= 0 && tag < calls.size()) {
            Array targets = Dictionary(calls[tag])["semantic_targets"];
            if (!targets.has(semantic))
                targets.append(semantic);
        } else if (tag < 0 && -tag <= samples.size()) {
            Array targets = Dictionary(samples[-tag - 1])["semantic_targets"];
            if (!targets.has(semantic))
                targets.append(semantic);
        }
    }
    Dictionary cs = value.get("component_tags", Dictionary());
    Array keys = cs.keys();
    for (int c = 0; c < keys.size(); ++c) {
        Array tags = cs[keys[c]];
        for (int i = 0; i < tags.size(); ++i) {
            int64_t tag = tags[i];
            if (tag >= 0 && tag < calls.size()) {
                Array targets = Dictionary(calls[tag])["semantic_targets"];
                String destination = semantic + "." + String(keys[c]);
                if (!targets.has(destination))
                    targets.append(destination);
            }
        }
    }
}
Array NBNativeRNGContract::statements(const String &body, int64_t first_line) {
    Array result;
    String pending;
    int64_t start = first_line, number = first_line - 1;
    PackedStringArray lines = body.split("\n");
    for (int i = 0; i < lines.size(); ++i) {
        ++number;
        String text = lines[i].strip_edges();
        if (text.is_empty() || text.begins_with("//"))
            continue;
        if (!pending.is_empty()) {
            pending += " " + text;
            if (text.ends_with(";")) {
                Dictionary row;
                row["text"] = pending;
                row["line"] = start;
                result.append(row);
                pending = "";
            }
            continue;
        }
        bool control = text.begins_with("if") || text.begins_with("else") || text.begins_with("for") ||
                       text.begins_with("while") || text.begins_with("switch") || text.begins_with("{") ||
                       text.begins_with("}");
        if (!control && !text.ends_with(";") && text.contains("=")) {
            pending = text;
            start = number;
        } else {
            Dictionary row;
            row["text"] = text;
            row["line"] = number;
            result.append(row);
        }
    }
    if (!pending.is_empty()) {
        Dictionary row;
        row["text"] = pending;
        row["line"] = start;
        result.append(row);
    }
    return result;
}
void NBNativeRNGContract::record_rng(const String &primitive, const Array &args, const String &type,
                                     const Dictionary &environment, const Array &stack, int64_t line_number,
                                     bool under_control, bool tick_seed) {
    Array seeds;
    for (int i = 0; i < args.size(); ++i)
        seeds.append(resolve(args[i], environment));
    int64_t start = unseeded_counter + 1, draws = primitive == "rand_float" ? components(type) : 1;
    if (seeds.size() == 4)
        ++counter;
    else if (seeds.size() == 1)
        unseeded_counter += draws;
    Dictionary call;
    call["primitive"] = primitive;
    call["components"] = components(type);
    call["counter"] = seeds.size() == 4 ? counter : 0;
    call["random_mode"] = seeds.size() == 4 ? "seeded" : "unseeded";
    call["call_id"] = Variant();
    call["seed1_expression"] = "";
    call["seed2_expression"] = "";
    call["seed3_expression"] = "";
    call["range_expression"] = seeds.is_empty() ? Variant("") : seeds[0];
    call["call_stack"] = stack.duplicate();
    call["semantic_targets"] = Array();
    call["parameter_uses"] = Array();
    call["line"] = line_number;
    if (seeds.size() == 1) {
        call["unseeded_call_index"] = start;
        call["unseeded_draw_count"] = draws;
    } else if (seeds.size() == 4) {
        call["seed1_expression"] = seeds[1];
        call["seed2_expression"] = seeds[2];
        call["seed3_expression"] = seeds[3];
        String second = seeds[2];
        if (second.is_valid_int())
            call["call_id"] = second.to_int();
        else if (!second.ends_with(".Emitter.RandomSeed") &&
                 !(tick_seed && second.ends_with(".Engine.System.TickCount")))
            error("Nonconstant CallID/seed2 requires a runtime RNG input expression: " + second, line_number);
    } else
        error("Unknown native RNG signature.", line_number);
    if (under_control)
        error("RNG inside conditional/loop flow requires a compiled control-flow implementation.",
              line_number);
    if (tick_seed)
        call["verified_tick_seed"] = true;
    calls.append(call);
}
} // namespace godot
