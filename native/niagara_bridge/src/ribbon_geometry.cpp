#include "ribbon_geometry.h"
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/sort_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace godot {
namespace {
constexpr double PI = 3.14159265358979323846;
Variant input(const Ref<Resource> &source, const StringName &key, const Variant &fallback) {
    return source.is_valid() ? source->call("get_input", key, fallback) : fallback;
}
String enum_name(const Variant &value) {
    if (value.get_type() == Variant::DICTIONARY) {
        Dictionary row = value;
        if (row.get("kind", "") == Variant("reference"))
            return row.get("name", "");
    }
    return String(value);
}
double lerp(double a, double b, double weight) {
    return a + (b - a) * weight;
}
Vector3 to_godot(const Vector3 &p) {
    return Vector3(p.y, p.z, -p.x) * .01f;
}
} // namespace
void NBNativeRibbonGeometry::_bind_methods() {
    ClassDB::bind_static_method("NBNativeRibbonGeometry",D_METHOD("resident_history","view","unique_id_order","tension"),&NBNativeRibbonGeometry::resident_history);
    ClassDB::bind_static_method("NBNativeRibbonGeometry", D_METHOD("settings", "source", "key"), &NBNativeRibbonGeometry::settings);
    ClassDB::bind_static_method("NBNativeRibbonGeometry", D_METHOD("errors", "source"), &NBNativeRibbonGeometry::errors);
    ClassDB::bind_static_method("NBNativeRibbonGeometry", D_METHOD("_acos_fast", "value"),
                                &NBNativeRibbonGeometry::acos_fast);
    ClassDB::bind_static_method("NBNativeRibbonGeometry",
                                D_METHOD("topology", "particles", "unique_id_order", "tension"),
                                &NBNativeRibbonGeometry::topology);
    ClassDB::bind_static_method("NBNativeRibbonGeometry", D_METHOD("update_history", "data", "history"),
                                &NBNativeRibbonGeometry::update_history);
    ClassDB::bind_static_method(
        "NBNativeRibbonGeometry",
        D_METHOD("tessellation", "data", "source", "history", "distance_cm", "advance_history"),
        &NBNativeRibbonGeometry::tessellation);
    ClassDB::bind_static_method("NBNativeRibbonGeometry", D_METHOD("_uv_parameters", "uv", "data"),
                                &NBNativeRibbonGeometry::uv_parameters);
    ClassDB::bind_static_method("NBNativeRibbonGeometry", D_METHOD("_uv_vector", "value", "fallback"),
                                &NBNativeRibbonGeometry::uv_vector);
    ClassDB::bind_static_method(
        "NBNativeRibbonGeometry",
        D_METHOD("build", "particles", "source", "space", "camera", "history", "advance_history"),
        &NBNativeRibbonGeometry::build);
}
double NBNativeRibbonGeometry::acos_fast(double value) {
    double absolute = std::abs(value),
           result = (-.156583 * absolute + PI * .5) * std::sqrt(std::max(0., 1. - absolute));
    return value >= 0. ? result : PI - result;
}
namespace {
struct RibbonPoint { Dictionary particle; Vector3 position; int64_t id=0; double age=0.; };
struct RibbonOrder {
    bool unique=true;
    bool operator()(const RibbonPoint &a,const RibbonPoint &b) const {return unique ? a.id>b.id : a.age<b.age;}
};
Dictionary topology_rows(std::vector<RibbonPoint> sorted,bool unique_id_order,double tension) {
    tension=std::min(tension,.99);
    SortArray<RibbonPoint,RibbonOrder> sorter;sorter.compare.unique=unique_id_order;
    if(!sorted.empty()) sorter.sort(sorted.data(),sorted.size());
    Dictionary result;
    Array points;
    PackedVector3Array tangents;
    PackedFloat32Array distances;
    result["points"] = points;
    result["tangents"] = tangents;
    result["distances"] = distances;
    double total = 0, weighted_length = 0, weighted_angle = 0, stat_length = 0;
    auto finish = [&]() {
        result["tangents"] = tangents;
        result["distances"] = distances;
        result["length"] = total;
        result["weighted_length"] = weighted_length;
        result["weighted_angle"] = weighted_angle;
        result["stat_length"] = stat_length;
        return result;
    };
    if (sorted.size() < 2)
        return finish();
    Vector3 first = sorted[0].position, previous_vector;
    double previous_length = 0;
    int current = 1;
    while (current < int(sorted.size())) {
        previous_vector = Vector3(sorted[current].position) - first;
        previous_length = previous_vector.length();
        if (previous_length > 1.)
            break;
        previous_length = 0.;
        ++current;
    }
    if (previous_length <= 0.)
        return finish();
    previous_vector /= previous_length;
    points.append(sorted[0].particle);
    tangents.append(previous_vector);
    distances.append(0);
    for (int next = current + 1; next < int(sorted.size()); ++next) {
        Vector3 next_vector = Vector3(sorted[next].position) - Vector3(sorted[current].position);
        double next_length = next_vector.length();
        if (next_length <= 1. && next != int(sorted.size()) - 1)
            continue;
        next_vector /= std::max(1., next_length);
        total += previous_length;
        points.append(sorted[current].particle);
        tangents.append((previous_vector + next_vector).normalized() * (1. - tension));
        distances.append(total);
        stat_length += next_length;
        weighted_length += next_length * next_length;
        weighted_angle += next_length * NBNativeRibbonGeometry::acos_fast(previous_vector.dot(next_vector));
        current = next;
        previous_vector = next_vector;
        previous_length = next_length;
    }
    if (previous_length > 0.) {
        total += previous_length;
        points.append(sorted[current].particle);
        tangents.append(previous_vector);
        distances.append(total);
    }
    if (points.size() > 2) {
        Vector3 first_tangent = tangents[0], second = tangents[1];
        tangents.set(0, 2. * first_tangent.dot(second) * first_tangent - second);
        Vector3 last = tangents[tangents.size() - 1], penultimate = tangents[tangents.size() - 2];
        tangents.set(tangents.size() - 1, 2. * last.dot(penultimate) * last - penultimate);
    }
    return finish();
}
} // namespace
Dictionary NBNativeRibbonGeometry::topology(const Array &particles,bool unique_id_order,double tension) {
    std::vector<RibbonPoint> rows;rows.reserve(particles.size());
    for(int64_t i=0;i<particles.size();++i) {
        Dictionary p=particles[i];rows.push_back({p,p["position"],p.get("id",0),p.get("normalized_age",0.)});
    }
    return topology_rows(std::move(rows),unique_id_order,tension);
}
Dictionary NBNativeRibbonGeometry::resident_history(const Ref<NBNativeParticleView> &view,bool unique_id_order,double tension) {
    std::vector<RibbonPoint> rows;
    if(view.is_valid() && view->get_errors().is_empty()) {
        rows.reserve(view->get_count());int64_t position=view->field_handle("position"),id=view->field_handle("id"),age=view->field_handle("normalized_age");
        for(int64_t i=0;i<view->get_count();++i) rows.push_back({Dictionary(),view->read(position,i),view->read(id,i),view->read(age,i)});
    }
    Dictionary result=topology_rows(std::move(rows),unique_id_order,tension);
    result.erase("points");return result;
}
void NBNativeRibbonGeometry::update_history(const Dictionary &data, Dictionary history) {
    double length = data.get("stat_length", 0.);
    if (length > 0.) {
        double angle = double(data["weighted_angle"]) / std::max(1., length);
        double curvature = double(data["weighted_length"]) / std::max(1., length) /
                           std::max(1e-8, std::abs(std::sin(angle)));
        double old = history.get("length", 0.), blend = old / (length + old);
        history["angle"] = lerp(angle, std::max(double(history.get("angle", 0.)), angle), blend);
        history["curvature"] =
            lerp(curvature, std::max(double(history.get("curvature", 0.)), curvature), blend);
        history["length"] = length;
    } else {
        history["angle"] = 0.;
        history["curvature"] = 0.;
        history["length"] = 0.;
    }
}
int NBNativeRibbonGeometry::tessellation(const Dictionary &data, const Ref<Resource> &source,
                                         Dictionary history, double distance_cm, bool advance_history) {
    if (advance_history)
        update_history(data, history);
    String mode = enum_name(input(source, "TessellationMode", "Automatic"));
    if (mode == "Disabled")
        return 1;
    int maximum = 16;
    double minimum_angle = 15. * PI / 180., screen_percentage = .002;
    if (mode == "Custom") {
        maximum = std::clamp(int(int64_t(input(source, "TessellationFactor", 16))), 1, 16);
        if (bool(input(source, "bUseConstantFactor", false)))
            return maximum;
        double angle = input(source, "TessellationAngle", 15.);
        minimum_angle = (angle > 0. && angle < 1. ? 1. : angle) * PI / 180.;
        if (!bool(input(source, "bScreenSpaceTessellation", true)))
            screen_percentage = 0.;
    }
    double curvature = history.get("curvature", 0.);
    if (curvature <= 1e-8)
        return 1;
    double angle = history.get("angle", 0.),
           minimum = minimum_angle == 0. ? maximum : std::max(1., angle / std::max(1e-8, minimum_angle));
    double displacement = std::max(.5, screen_percentage * distance_cm);
    double factor = angle / std::max(.002, acos_fast(curvature / (curvature + displacement)));
    return std::clamp(int(std::floor(factor + .5)), std::min(maximum, int(std::floor(minimum + .5))),
                      maximum);
}
Vector2 NBNativeRibbonGeometry::uv_vector(const Variant &value, const Vector2 &fallback) {
    if (value.get_type() == Variant::DICTIONARY) {
        Dictionary v = value;
        return Vector2(double(v.get("x", fallback.x)), double(v.get("y", fallback.y)));
    }
    if (value.get_type() == Variant::VECTOR2)
        return value;
    if (value.get_type() == Variant::ARRAY && Array(value).size() >= 2) {
        Array v = value;
        return Vector2(double(v[0]), double(v[1]));
    }
    if (value.get_type() == Variant::INT || value.get_type() == Variant::FLOAT)
        return Vector2(1, 1) * double(value);
    return fallback;
}
Vector3 NBNativeRibbonGeometry::uv_parameters(const Dictionary &uv, const Dictionary &data) {
    Array points = data["points"];
    PackedFloat32Array distances = data["distances"];
    if (points.size() < 2)
        return Vector3();
    double leading = 0, trailing = 0;
    auto age = [&](int n) { return double(Dictionary(points[n])["normalized_age"]); };
    if (uv.get("leadingEdgeMode", "Locked") == Variant("SmoothTransition")) {
        double interval = age(1) - age(0);
        if (interval > 0)
            leading = std::max(0., interval - age(0)) / interval;
    }
    if (uv.get("trailingEdgeMode", "Locked") == Variant("SmoothTransition")) {
        double interval = age(points.size() - 1) - age(points.size() - 2);
        if (interval > 0)
            trailing = std::max(0., interval - (1. - age(points.size() - 1))) / interval;
    }
    String mode = uv.get("distributionMode", "ScaledUsingRibbonSegmentLength");
    double length = data["length"], leading_length = distances[1] * leading;
    double trailing_length = (distances[distances.size() - 1] - distances[distances.size() - 2]) * trailing;
    double scale, offset, distribution = 1. / length;
    if (mode == "ScaledUniformly") {
        double segments = points.size() - 1.;
        if (segments - leading - trailing == 0)
            return Vector3(INFINITY, INFINITY, INFINITY);
        scale = segments / (segments - leading - trailing);
        offset = -leading / segments * scale;
        distribution = 1. / segments;
    } else if (mode == "TiledOverRibbonLength") {
        double tile = uv.get("tilingLength", 100.);
        scale = length / tile;
        offset = -leading_length / tile;
    } else {
        if (length - leading_length - trailing_length == 0)
            return Vector3(INFINITY, INFINITY, INFINITY);
        scale = length / (length - leading_length - trailing_length);
        offset = -leading_length / length * scale;
    }
    Vector2 authored_scale = uv_vector(uv.get("scale", Dictionary()), Vector2(1, 1)),
            authored_offset = uv_vector(uv.get("offset", Dictionary()), Vector2());
    return Vector3(scale * authored_scale.x, offset * authored_scale.x + authored_offset.x, distribution);
}
Dictionary NBNativeRibbonGeometry::build(const Array &particles, const Ref<Resource> &source,
                                         const Transform3D &space, Camera3D *camera, Dictionary history,
                                         bool advance_history) {
    Dictionary data = topology(particles, bool(input(source, "bLinkOrderUseUniqueID", true)),
                               double(input(source, "CurveTension", 0.)));
    Array points = data["points"];
    Dictionary result;
    result["arrays"] = Array();
    result["points"] = points.size();
    result["segments"] = std::max(int(points.size()) - 1, 0);
    result["subdivisions"] = 1;
    if (points.size() < 2)
        return result;
    std::array<Dictionary, 2> uv{settings(source, "UV0Settings"), settings(source, "UV1Settings")};
    std::array<Vector3, 2> uv_params{uv_parameters(uv[0], data), uv_parameters(uv[1], data)};
    std::array<Vector2, 2> uv_scale, uv_offset;
    std::array<bool, 2> uniform_distribution;
    for (int c = 0; c < 2; ++c) {
        uv_scale[c] = uv_vector(uv[c].get("scale", Dictionary()), Vector2(1, 1));
        uv_offset[c] = uv_vector(uv[c].get("offset", Dictionary()), Vector2());
        uniform_distribution[c] = uv[c].get("distributionMode", "") == Variant("ScaledUniformly");
    }
    struct Sample {
        Vector3 position;
        Vector3 facing;
        bool has_facing = false;
        double age = 0, width = 1;
        Color color;
        std::array<Color, 4> dynamic;
    };
    std::vector<Sample> samples;
    samples.reserve(points.size());
    AABB bounds(space.xform(to_godot(Dictionary(points[0])["position"])), Vector3());
    for (int n = 0; n < points.size(); ++n) {
        Dictionary point = points[n];
        Sample s;
        s.position = point["position"];
        s.has_facing = point.has("ribbon_facing");
        if (s.has_facing)
            s.facing = point["ribbon_facing"];
        s.age = point["normalized_age"];
        s.width = point.get("ribbon_width", 1.);
        s.color = point["color"];
        s.dynamic[0] = point["dynamic"];
        for (int c = 1; c < 4; ++c)
            s.dynamic[c] = point.get(String("dynamic") + String::num_int64(c), Color(0, 0, 0, 0));
        samples.push_back(s);
        bounds.expand_to(space.xform(to_godot(s.position)));
    }
    Vector3 camera_position = camera ? camera->get_global_position() : Vector3();
    double distance = 0;
    if (camera) {
        Vector3 closest = camera_position.clamp(bounds.position, bounds.get_end());
        distance = camera_position.distance_to(closest) * 100.;
    }
    int subdivisions = tessellation(data, source, history, distance, advance_history);
    result["subdivisions"] = subdivisions;
    int width_segments = input(source, "WidthSegmentationCount", 1);
    if (width_segments < 1 || width_segments > 16)
        return result;
    int64_t slices = (points.size() - 1) * subdivisions + 1, vertex_count = slices * (width_segments + 1);
    if (vertex_count > INT32_MAX / 16)
        return result;
    PackedVector3Array vertices, normals;
    PackedFloat32Array tangents, relative_times;
    PackedColorArray colors;
    std::array<PackedVector2Array, 2> texcoords;
    std::array<PackedFloat32Array, 4> custom;
    vertices.resize(vertex_count);
    normals.resize(vertex_count);
    tangents.resize(vertex_count * 4);
    colors.resize(vertex_count);
    relative_times.resize(vertex_count);
    std::array<Vector2 *, 2> out_uv;
    std::array<float *, 4> out_custom;
    for (int c = 0; c < 2; ++c) {
        texcoords[c].resize(vertex_count);
        out_uv[c] = texcoords[c].ptrw();
    }
    for (int c = 0; c < 4; ++c) {
        custom[c].resize(vertex_count * 4);
        out_custom[c] = custom[c].ptrw();
    }
    Vector3 *out_vertices = vertices.ptrw(), *out_normals = normals.ptrw();
    float *out_tangents = tangents.ptrw(), *out_time = relative_times.ptrw();
    Color *out_color = colors.ptrw();
    Basis direction_basis(space.basis.get_column(0).normalized(), space.basis.get_column(1).normalized(),
                          space.basis.get_column(2).normalized());
    PackedVector3Array curve_tangents = data["tangents"];
    PackedFloat32Array distances = data["distances"];
    bool geometry_normals = input(source, "bUseGeometryNormals", false);
    String facing_mode = enum_name(input(source, "FacingMode", "Screen"));
    for (int slice = 0; slice < slices; ++slice) {
        int segment = std::min(slice / subdivisions, int(points.size()) - 2);
        double alpha = double(slice - segment * subdivisions) / subdivisions, a2 = alpha * alpha,
               a3 = a2 * alpha;
        const Sample &left = samples[segment], &right = samples[segment + 1];
        double length = distances[segment + 1] - distances[segment];
        Vector3 t0 = curve_tangents[segment], t1 = curve_tangents[segment + 1];
        Vector3 center = (2 * a3 - 3 * a2 + 1) * left.position + (a3 - 2 * a2 + alpha) * t0 * length +
                         (-2 * a3 + 3 * a2) * right.position + (a3 - a2) * t1 * length;
        center = space.xform(to_godot(center));
        Vector3 direction = direction_basis.xform(to_godot(t0.lerp(t1, alpha))).normalized();
        Vector3 facing = camera ? (camera_position - center).normalized() : Vector3(0, 0, 1);
        if (facing_mode != "Screen" && left.has_facing && right.has_facing) {
            Vector3 authored = left.facing.lerp(right.facing, alpha);
            facing = direction_basis.xform(Vector3(authored.y, authored.z, -authored.x)).normalized();
        }
        Vector3 side = direction.cross(facing).normalized();
        if (facing_mode == "CustomSideVector") {
            Vector3 custom_side = -facing;
            facing = side;
            side = custom_side;
        }
        Vector3 normal = (geometry_normals ? -direction.cross(side) : facing).normalized();
        if (facing_mode != "Screen" && normal.is_zero_approx())
            normal = Vector3(0, 1, 0); // Source safe-normalization fallback is world +Z.
        double width = lerp(left.width, right.width, alpha) * .01;
        Color color = left.color.lerp(right.color, alpha);
        std::array<Color, 4> dynamic;
        for (int c = 0; c < 4; ++c)
            dynamic[c] = left.dynamic[c].lerp(right.dynamic[c], alpha);
        double along = lerp(distances[segment], distances[segment + 1], alpha);
        for (int across = 0; across <= width_segments; ++across) {
            double v = double(across) / width_segments;
            int index = slice * (width_segments + 1) + across;
            out_vertices[index] = center + side * (v - .5) * width;
            out_normals[index] = normal;
            out_tangents[index * 4] = direction.x;
            out_tangents[index * 4 + 1] = direction.y;
            out_tangents[index * 4 + 2] = direction.z;
            out_tangents[index * 4 + 3] = -1.f;
            out_color[index] = color;
            out_time[index] = lerp(left.age, right.age, alpha);
            for (int c = 0; c < 4; ++c) {
                out_custom[c][index * 4] = dynamic[c].r;
                out_custom[c][index * 4 + 1] = dynamic[c].g;
                out_custom[c][index * 4 + 2] = dynamic[c].b;
                out_custom[c][index * 4 + 3] = dynamic[c].a;
            }
            for (int c = 0; c < 2; ++c) {
                double u = uniform_distribution[c] ? segment + alpha : along;
                out_uv[c][index] = Vector2(u * uv_params[c].z * uv_params[c].x + uv_params[c].y,
                                           v * uv_scale[c].y + uv_offset[c].y);
            }
        }
    }
    bool reverse = false;
    if (camera) {
        Vector3 forward = -camera->get_global_basis().get_column(2);
        double start = forward.dot(to_godot(samples.front().position) - camera_position),
               end = forward.dot(to_godot(samples.back().position) - camera_position);
        bool back_to_front = enum_name(input(source, "DrawDirection", "FrontToBack")) == "BackToFront";
        reverse = (start >= end && back_to_front) || (start < end && !back_to_front);
    }
    PackedInt32Array indices;
    indices.resize((points.size() - 1) * subdivisions * width_segments * 6);
    int32_t *out_indices = indices.ptrw();
    int at = 0;
    for (int order = 0; order < points.size() - 1; ++order) {
        int segment = reverse ? int(points.size()) - 2 - order : order;
        for (int sub = 0; sub < subdivisions; ++sub) {
            int base = (segment * subdivisions + sub) * (width_segments + 1);
            for (int across = 0; across < width_segments; ++across) {
                int a = base + across, b = a + 1, c = a + width_segments + 1, d = c + 1;
                for (int index : {a, c, b, b, c, d})
                    out_indices[at++] = index;
            }
        }
    }
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = vertices;
    arrays[Mesh::ARRAY_NORMAL] = normals;
    arrays[Mesh::ARRAY_TANGENT] = tangents;
    arrays[Mesh::ARRAY_COLOR] = colors;
    arrays[Mesh::ARRAY_TEX_UV] = texcoords[0];
    arrays[Mesh::ARRAY_TEX_UV2] = texcoords[1];
    for (int c = 0; c < 4; ++c)
        arrays[Mesh::ARRAY_CUSTOM0 + c] = custom[c];
    arrays[Mesh::ARRAY_INDEX] = indices;
    result["arrays"] = arrays;
    result["relative_times"] = relative_times;
    return result;
}
} // namespace godot
