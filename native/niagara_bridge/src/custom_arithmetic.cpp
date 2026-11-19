#include "material_compiler.h"
#include <godot_cpp/classes/reg_ex.hpp>
#include <godot_cpp/classes/reg_ex_match.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <cmath>
namespace godot {
namespace {
using namespace nbmaterial;
struct Arithmetic {
    PackedStringArray tokens;
    int64_t position = 0, depth = 0;
    Dictionary inputs, locals;
    PackedStringArray statements;
    String local_prefix;
    String error;
    bool uses_pseudorandom = false;
    String peek(int ahead = 0) {
        return position + ahead < tokens.size() ? tokens[position + ahead] : String();
    }
    String take() {
        String value = peek();
        ++position;
        return value;
    }
    void fail(const String &message) {
        if (error.is_empty())
            error = message;
    }
    void expect(const String &token) {
        if (peek() != token)
            fail("Expected '" + token + String("'."));
        else
            take();
    }
    Dictionary zero() {
        return dict({{"code", "0"}, {"type", "int"}, {"channels", 1}});
    }
    Dictionary cast(const Dictionary &value, const String &target) {
        if (width(value) != 1)
            fail("Arithmetic casts require an explicit scalar input/swizzle.");
        return dict({{"code", target + String("(") + code(value) + ")"}, {"type", target}, {"channels", 1}});
    }
    Dictionary binary(const Dictionary &left, const Dictionary &right, const String &op) {
        int lanes = MAX(width(left), width(right));
        if (lanes > 1) {
            if (op != "+" && op != "-" && op != "*" && op != "/") {
                fail("Unsupported vector arithmetic operator: " + op);
                return zero();
            }
            auto a = float_value(left, lanes), b = float_value(right, lanes);
            return expression_value("(" + code(a) + " " + op + " " + code(b) + ")", lanes);
        }
        String a = text(left, "type"), b = text(right, "type"),
               target = (a == "float" || b == "float")
                            ? String("float")
                            : ((a == "uint" || b == "uint") ? String("uint") : String("int"));
        if (a == "bool" || b == "bool")
            fail("Numeric operands are required.");
        if (op == "%" && target == "float")
            fail("Floating remainder requires a separate verified lowering.");
        Dictionary x = cast(left, target), y = cast(right, target);
        return dict({{"code", "(" + code(x) + " " + op + String(" ") + code(y) + ")"},
                     {"type", precedence(op) <= 2 ? String("bool") : target},
                     {"channels", 1}});
    }
    int precedence(const String &token) {
        if (token == "==" || token == "!=") return 1;
        if (token == "<" || token == ">" || token == "<=" || token == ">=") return 2;
        if (token == "+" || token == "-") return 3;
        if (token == "*" || token == "/" || token == "%") return 4;
        return 0;
    }
    Dictionary float_value(const Dictionary &value, int channels) {
        if ((width(value) != channels && width(value) != 1) || text(value, "type") == "bool")
            fail("Incompatible numeric widths or types.");
        String type = channels == 1 ? String("float") : "vec" + String::num_int64(channels);
        Dictionary result = expression_value(type + "(" + code(value) + ")", channels);
        return result;
    }
    Dictionary expression_value(const String &source, int channels) {
        return dict({{"code", source}, {"type", "float"}, {"channels", channels}});
    }
    Dictionary conditional(const Dictionary &condition, const Dictionary &a, const Dictionary &b) {
        if (width(condition) != 1)
            fail("Conditional requires a scalar test.");
        String test = code(condition);
        if (text(condition, "type") != "bool")
            test = "(" + code(cast(condition, "float")) + " != 0.0)";
        int channels = MAX(width(a), width(b));
        if (text(a, "type") == text(b, "type") && width(a) == width(b)) {
            Dictionary result = a.duplicate();
            result.erase("constant");
            result["code"] = "(" + test + " ? " + code(a) + " : " + code(b) + ")";
            return result;
        }
        if (channels == 1 && text(a, "type") != "float" && text(b, "type") != "float" &&
            text(a, "type") != "bool" && text(b, "type") != "bool")
            return dict({{"code", "(" + test + " ? " + code(cast(a, "uint")) + " : " + code(cast(b, "uint")) + ")"},
                         {"type", "uint"}, {"channels", 1}});
        return expression_value("(" + test + " ? " + code(float_value(a, channels)) + " : " +
                                code(float_value(b, channels)) + ")", channels);
    }
    Dictionary expression(int minimum) {
        ++depth;
        if (depth > 64) {
            fail("Expression nesting exceeds 64 levels.");
            --depth;
            return zero();
        }
        Dictionary left = primary();
        while (error.is_empty() && precedence(peek()) > minimum) {
            String operation = take();
            Dictionary right = expression(precedence(operation));
            left = binary(left, right, operation);
        }
        if (minimum == 0 && error.is_empty() && peek() == "?") {
            take();
            Dictionary a = expression(0);
            expect(":");
            Dictionary b = expression(0);
            left = conditional(left, a, b);
        }
        --depth;
        return left;
    }
    bool scalar_type(const String &name) {
        return name == "uint" || name == "int" || name == "float";
    }
    int vector_width(const String &name) {
        return name == "float2" ? 2 : (name == "float3" ? 3 : (name == "float4" ? 4 : 0));
    }
    Dictionary constructor(int channels) {
        expect("(");
        PackedStringArray args;
        int count = 0;
        do {
            if (!args.is_empty()) take();
            Dictionary arg = expression(0);
            int size = width(arg);
            args.append(code(float_value(arg, size)));
            count += size;
        } while (peek() == "," && error.is_empty());
        expect(")");
        if (count != channels) fail("Vector constructor has the wrong number of components.");
        return expression_value("vec" + String::num_int64(channels) + "(" + String(", ").join(args) + ")", channels);
    }
    void declarations() {
        while (error.is_empty() && (scalar_type(peek()) || vector_width(peek())) && peek(1).is_valid_identifier()) {
            String type = take(), name = take();
            if (inputs.has(name) || locals.has(name) || scalar_type(name) || vector_width(name) ||
                name == "return" || name == "saturate" || name == "Mod")
                fail("Duplicate or reserved local identifier: " + name);
            expect("=");
            Dictionary value = expression(0);
            expect(";");
            int channels = vector_width(type);
            value = channels ? float_value(value, channels) : cast(value, type);
            String local = local_prefix + String::num_int64(locals.size());
            String shader_type = channels ? "vec" + String::num_int64(channels) : type;
            statements.append(shader_type + " " + local + " = " + code(value) + ";");
            value["code"] = local;
            locals[name] = value;
        }
    }
    Dictionary primary() {
        if (!error.is_empty())
            return zero();
        String token = take();
        Dictionary value = zero();
        if (token == "(" && scalar_type(peek()) && peek(1) == ")") {
            String target = take();
            take();
            value = cast(expression(4), target);
        } else if (token == "(") {
            value = expression(0);
            expect(")");
        } else if (token == "+" || token == "-") {
            value = expression(4);
            value["code"] = "(" + token + code(value) + ")";
        } else if (vector_width(token)) {
            value = constructor(vector_width(token));
        } else if (token == "PseudoRandom") {
            expect("(");
            Dictionary arg = expression(0);
            expect(")");
            if (width(arg) != 2) fail("PseudoRandom requires float2 coordinates.");
            uses_pseudorandom = true;
            value = expression_value("nb_pseudorandom(" + code(arg) + ")", 1);
        } else if (token == "saturate") {
            expect("(");
            Dictionary arg = expression(0);
            expect(")");
            value = expression_value("clamp(" + code(float_value(arg, width(arg))) + ", 0.0, 1.0)", width(arg));
        } else if (scalar_type(token)) {
            expect("(");
            value = cast(expression(0), token);
            expect(")");
        } else if (token == "Mod") {
            expect("(");
            Dictionary left = cast(expression(0), "uint");
            expect(",");
            Dictionary right = cast(expression(0), "uint");
            expect(")");
            value = binary(left, right, "%");
        } else if (locals.has(token)) {
            value = Dictionary(locals[token]).duplicate();
        } else if (inputs.has(token)) {
            value = Dictionary(inputs[token]).duplicate();
            value["type"] = "float";
            if (width(value) < 1 || width(value) > 4)
                fail("Custom arithmetic input must be numeric.");
        } else if (token.contains(".") && token.is_valid_float()) {
            if (!std::isfinite(static_cast<float>(token.to_float())))
                fail("Floating literal exceeds its 32-bit type.");
            value = expression_value(token, 1);
        } else if (!token.is_empty() && token[0] >= '0' && token[0] <= '9') {
            bool unsigned_value = token.ends_with("u") || token.ends_with("U");
            String digits = unsigned_value ? token.left(-1) : token;
            if (!digits.is_valid_int() || digits.to_int() > (unsigned_value ? 4294967295LL : 2147483647LL))
                fail("Integer literal is outside its 32-bit type.");
            value = dict({{"code", String::num_int64(digits.to_int()) + (unsigned_value ? "u" : "")},
                          {"type", unsigned_value ? "uint" : "int"},
                          {"channels", 1}});
        } else
            fail("Unsupported identifier or expression: " + token);
        if (peek() == ".") {
            take();
            String component = take();
            String xyzw = "xyzw", rgba = "rgba";
            String alphabet = xyzw.contains(component.left(1)) ? xyzw : rgba;
            bool valid = width(value) >= 2 && component.length() >= 1 && component.length() <= 4;
            for (int i = 0; i < component.length(); ++i) {
                int k = alphabet.find(component.substr(i, 1));
                valid &= k >= 0 && k < width(value);
            }
            if (!valid) fail("Invalid vector swizzle: " + component);
            value["code"] = "(" + code(value) + ")." + component;
            value["channels"] = component.length();
        }
        return value;
    }
};
} // namespace
Dictionary NBNativeCustomArithmetic::compile(const String &source, const Dictionary &inputs) {
    return compile_typed(source, inputs, 1, "nb_custom_local_");
}
Dictionary NBNativeCustomArithmetic::compile_typed(const String &source, const Dictionary &inputs,
                                                   int channels, const String &local_prefix) {
    Arithmetic context;
    context.inputs = inputs;
    context.local_prefix = local_prefix;
    if (source.length() > 4096)
        return nbdocument::dict({{"error", "Expression exceeds 4096 characters."}});
    Ref<RegEx> pattern;
    pattern.instantiate();
    pattern->compile("\\s+|[A-Za-z_][A-Za-z_0-9]*|[0-9]+\\.[0-9]+|[0-9]+[uU]?|!=|==|<=|>=|[<>=?:()+*/%,.;-]");
    int64_t cursor = 0;
    while (cursor < source.length()) {
        Ref<RegExMatch> found = pattern->search(source, cursor);
        if (found.is_null() || found->get_start() != cursor)
            return nbdocument::dict({{"error", "Unsupported token at character " + String::num_int64(cursor)}});
        String token = found->get_string();
        if (!token.strip_edges().is_empty())
            context.tokens.append(token);
        cursor = found->get_end();
    }
    context.declarations();
    if (context.peek() == "return")
        context.take();
    else if (!context.statements.is_empty())
        context.fail("Local declarations must be followed by a return.");
    Dictionary result = context.expression(0);
    if (context.peek() == ";")
        context.take();
    if (context.position != context.tokens.size())
        context.fail("Only one arithmetic expression is supported.");
    if (nbmaterial::width(result) != channels || nbmaterial::text(result, "type") == "bool")
        context.fail("Custom arithmetic result does not match the declared output.");
    if (!context.error.is_empty())
        return nbdocument::dict({{"error", context.error}});
    Dictionary output = nbmaterial::expression(channels == 1 ? "float(" + nbmaterial::code(result) + ")" :
                                                               nbmaterial::code(result), channels);
    if (!context.statements.is_empty())
        output["statements"] = context.statements;
    if (context.uses_pseudorandom)
        output["pseudorandom"] = true;
    return output;
}
void NBNativeCustomArithmetic::_bind_methods() {
    ClassDB::bind_static_method("NBNativeCustomArithmetic", D_METHOD("compile", "source", "inputs"),
                                &NBNativeCustomArithmetic::compile);
}
} // namespace godot
