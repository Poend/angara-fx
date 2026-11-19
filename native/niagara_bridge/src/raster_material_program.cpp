#include "material_programs.h"
#include "raster_material_program.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/reg_ex.hpp>
#include <godot_cpp/classes/reg_ex_match.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
namespace godot {
using namespace nbmaterial;
String NBNativeRasterProgram::source_key(const Ref<ShaderMaterial> &material) {
    bool system = material->has_meta("niagara_bridge_system_program");
    Dictionary data =
        material->get_meta(system ? "niagara_bridge_system_program" : "niagara_bridge_material_program");
    String source = text(data, system ? "context_sha256" : "graph_sha256");
    Array formats;
    if (system) {
        Array contexts = data["contexts"];
        for (int64_t i = 0; i < contexts.size(); ++i) {
            Dictionary context = contexts[i];
            Ref<ShaderMaterial> source = context["material"];
            Dictionary program = source->get_meta("niagara_bridge_material_program");
            formats.append(program["native_texture_formats"]);
        }
    } else
        formats.append(data["native_texture_formats"]);
    return NBNativeMaterialPrograms::shader_code_sha256(material->get_shader()) + ":" + source + String(":") +
           UtilityFunctions::var_to_str(formats).sha256_text();
}
Dictionary NBNativeRasterProgram::lower(const Dictionary &compiled, bool batch, const String &epilogue,
                                        const String &extra_header, const String &vertex_epilogue) {
    TypedArray<String> errors;
    Dictionary parts = compiled["parts"], support = parts["support"];
    String header = text(support, "header") + "\n" + join(parts["declarations"], "\n") + "\n" + extra_header;
    for (const char *name : {"particle_attributes", "vertex_colors"}) {
        String path = String("res://addons/niagara_bridge/materials/") + name + String(".gdshaderinc");
        header = header.replace("#include \"" + path + String("\""), FileAccess::get_file_as_string(path));
    }
    Ref<RegEx> branch;
    branch.instantiate();
    branch->compile("(?s)#ifdef NB_PARTICLE_CONTEXT_FROM_GEOMETRY\\n(.*?)#else\\n(.*?)#endif");
    header = branch->sub(header, batch ? "$1" : "$2", true);
    TypedArray<Dictionary> fields, samplers;
    Ref<RegEx> declarations;
    declarations.instantiate();
    declarations->compile("(?m)^uniform (\\w+) (\\w+)([^;]*);");
    TypedArray<RegExMatch> matches = declarations->search_all(header);
    for (int64_t i = 0; i < matches.size(); ++i) {
        Ref<RegExMatch> match = matches[i];
        String type = match->get_string(1), name = match->get_string(2);
        if (type == "sampler2D" || type == "sampler3D") {
            String hints = match->get_string(3).trim_prefix(" :").strip_edges();
            samplers.append(
                dict({{"name", name}, {"type", type}, {"hints", hints}, {"binding", samplers.size() + 1}}));
        } else {
            PackedStringArray supported = String("float,int,bool,vec2,vec3,vec4,mat3,mat4").split(",");
            if (!supported.has(type))
                errors.append("RD distortion uniform type is unsupported: " + type);
            fields.append(dict({{"name", name}, {"type", type}}));
        }
        header = header.replace(match->get_string(), "");
    }
    const char *names[] = {
        "PROJECTION_MATRIX", "INV_PROJECTION_MATRIX", "VIEW_MATRIX",         "INV_VIEW_MATRIX",
        "MODEL_MATRIX",      "MODELVIEW_MATRIX",      "MODEL_NORMAL_MATRIX", "MODELVIEW_NORMAL_MATRIX",
        "VIEWPORT_SIZE",     "nb_instance_color",     "INSTANCE_CUSTOM",     "INSTANCE_ID"};
    const char *types[] = {"mat4", "mat4", "mat4", "mat4", "mat4", "mat4",
                           "mat3", "mat3", "vec2", "vec4", "vec4", "int"};
    for (int i = 0; i < 12; ++i)
        fields.append(dict({{"name", names[i]}, {"type", types[i]}}));
    String aliases;
    int64_t slots = 0;
    for (int64_t i = 0; i < fields.size(); ++i) {
        Dictionary field = fields[i];
        field["slot"] = slots;
        String slot = "nb_parameters[" + String::num_int64(slots) + "]", type = field["type"], expression;
        if (type == "float")
            expression = slot + ".x";
        else if (type == "int")
            expression = "floatBitsToInt(" + slot + ".x)";
        else if (type == "bool")
            expression = "(floatBitsToInt(" + slot + ".x) != 0)";
        else if (type == "vec2")
            expression = slot + ".xy";
        else if (type == "vec3")
            expression = slot + ".xyz";
        else if (type == "vec4")
            expression = slot;
        else if (type == "mat3" || type == "mat4") {
            TypedArray<String> columns;
            for (int column = 0; column < (type == "mat3" ? 3 : 4); ++column)
                columns.append("nb_parameters[" + String::num_int64(slots + column) + "]" +
                               (type == "mat3" ? ".xyz" : ""));
            expression = type + "(" + join(columns, ",") + ")";
        }
        aliases += "#define " + text(field, "name") + " (" + expression + ")\n";
        slots += type == "mat3" ? 3 : (type == "mat4" ? 4 : 1);
    }
    String common = "#version 450\n#define CURRENT_RENDERER 1\n#define RENDERER_COMPATIBILITY 0\n#define "
                    "CAMERA_POSITION_WORLD (INV_VIEW_MATRIX[3].xyz)\nlayout(set=0,binding=0,std140) uniform "
                    "Parameters { vec4 nb_parameters[" +
                    String::num_int64(slots) + "]; };\n" + aliases;
    for (int64_t i = 0; i < samplers.size(); ++i) {
        Dictionary sampler = samplers[i];
        common += "layout(set=0,binding=" + String::num_int64(sampler["binding"]) + ") uniform " +
                  text(sampler, "type") + " " + text(sampler, "name") + ";\n";
    }
    Ref<RegEx> varying;
    varying.instantiate();
    varying->compile("(?m)^varying (flat )?(\\w+) (\\w+);");
    String vertex_header, fragment_header;
    int64_t location = 0;
    matches = varying->search_all(header);
    for (int64_t i = 0; i < matches.size(); ++i) {
        Ref<RegExMatch> match = matches[i];
        String declaration = match->get_string(1) + match->get_string(2) + " " + match->get_string(3) + ";\n";
        vertex_header += "layout(location=" + String::num_int64(location) + ") out " + declaration;
        fragment_header += "layout(location=" + String::num_int64(location) + ") in " + declaration;
        header = header.replace(match->get_string(), "");
        ++location;
    }
    const char *varying_types[] = {"vec2", "vec2", "vec4", "vec3", "vec3"},
               *varying_names[] = {"UV", "UV2", "COLOR", "nb_view_position", "nb_geometry_normal"};
    for (int i = 0; i < 5; ++i) {
        String declaration = String(varying_types[i]) + " " + varying_names[i] + ";\n";
        vertex_header += "layout(location=" + String::num_int64(location) + ") out " + declaration;
        fragment_header += "layout(location=" + String::num_int64(location) + ") in " + declaration;
        ++location;
    }
    Array vertex_lines = Array(support["vertex"]).duplicate();
    vertex_lines.append_array(parts["vertex"]);
    String vertex =
        common + vertex_header + header +
        "\nlayout(set=1,binding=0,std430) readonly buffer Geometry { vec4 records[]; };\n#define VERTEX_ID "
        "gl_VertexIndex\nvoid main() {\nint base=gl_VertexIndex*10; vec3 VERTEX=records[base].xyz; vec3 "
        "NORMAL=records[base+1].xyz; vec4 tangent=records[base+2]; vec3 TANGENT=tangent.xyz; vec3 "
        "BINORMAL=cross(NORMAL,TANGENT)*tangent.w;\nUV=records[base+3].xy; UV2=records[base+3].zw; "
        "COLOR=records[base+4]*nb_instance_color;\nvec4 "
        "CUSTOM0=records[base+5],CUSTOM1=records[base+6],CUSTOM2=records[base+7],CUSTOM3=records[base+8];\n" +
        join(vertex_lines, "\n") +
        "\nvec4 view_position=MODELVIEW_MATRIX*vec4(VERTEX,1.0); nb_view_position=view_position.xyz; "
        "nb_geometry_normal=normalize(MODELVIEW_NORMAL_MATRIX*NORMAL); "
        "gl_Position=PROJECTION_MATRIX*view_position;\n" +
        vertex_epilogue + "\n}\n";
    String fragment =
        common + fragment_header + header +
        "\nlayout(location=0) out vec4 out_color;\n#define FRONT_FACING gl_FrontFacing\n#define FRAGCOORD "
        "gl_FragCoord\n#define SCREEN_UV (gl_FragCoord.xy/VIEWPORT_SIZE)\nvoid main() {\nvec3 "
        "VERTEX=nb_view_position; vec3 NORMAL=normalize(nb_geometry_normal); vec3 VIEW=normalize(-VERTEX); "
        "vec3 ALBEDO=vec3(0.0),EMISSION=vec3(0.0); float ALPHA=1.0; float "
        "METALLIC=0.0,SPECULAR=0.5,ROUGHNESS=0.5,AO=1.0;\nif(gl_FragCoord.z < "
        "textureLod(nb_depth_texture,SCREEN_UV,0.0).r) discard;\n" +
        join(parts["fragment"], "\n") + "\n" + epilogue + "\n}\n";
    Array modes = parts["render_modes"];
    return dict({{"errors", errors},
                 {"vertex", vertex},
                 {"fragment", fragment},
                 {"fields", fields},
                 {"slots", slots},
                 {"samplers", samplers},
                 {"defaults", compiled["uniforms"]},
                 {"cull_disabled", modes.has("cull_disabled")}});
}
void NBNativeRasterProgram::_bind_methods() {
    ClassDB::bind_static_method("NBNativeRasterProgram", D_METHOD("source_key", "material"),
                                &NBNativeRasterProgram::source_key);
    ClassDB::bind_static_method(
        "NBNativeRasterProgram",
        D_METHOD("lower", "compiled", "batch", "epilogue", "extra_header", "vertex_epilogue"),
        &NBNativeRasterProgram::lower);
}
} // namespace godot
