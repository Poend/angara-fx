#include "vm_external.h"
#include "vm_scalar.h"
#include <algorithm>
#include <limits>

namespace nbvm {
using namespace godot;
namespace {
bool integer_value(const Variant &v, int64_t min, int64_t max) {
    return (v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT) &&
           std::floor(double(v)) == double(v) && double(v) >= min && double(v) <= max;
}
int32_t scaled_cells(int32_t n, float scale, int32_t maximum) {
    float scaled = float(n) * scale;
    if (scaled >= float(maximum))
        return maximum;
    return scaled > 1.f ? int32_t(scaled) : 1;
}
} // namespace
String ExternalBinding::bind_volume(const Dictionary &info, const Dictionary &signature) {
    Dictionary control = info.get("volume_control", Dictionary());
    const bool grid = info.get("type", Variant()) == Variant("NiagaraDataInterfaceGrid3DCollection");
    const String expected = grid ? "UNiagaraDataInterfaceGrid3DCollection.FindAttributes"
                                 : "UNiagaraDataInterfaceRenderTargetVolume";
    String key = info.get("resolved_parameter", "");
    Variant scale = control.get("scale", Variant());
    if (String(control.get("producer", "")) != expected || key.is_empty() || key == "None" ||
        (scale.get_type() != Variant::INT && scale.get_type() != Variant::FLOAT) ||
        !std::isfinite(float(double(scale))))
        return "Missing native volume control metadata for " + key;
    kind = VolumeControl;
    metadata["kind"] = "volume_control";
    metadata["parameter"] = key;
    metadata["scale"] = scale;
    const int inputs = int64_t(signature.get("num_inputs", 0));
    metadata["max_dimension"] = INT32_MAX;
    if (grid) {
        if (control.get("independent", Variant()) != Variant(true))
            return "Unsupported initial Grid3D resolution method: " + key;
        Vector3i initial;
        int axis = 0;
        for (const char *name : {"x", "y", "z"}) {
            Variant size = control.get(name, Variant());
            if (!integer_value(size, 1, INT32_MAX))
                return "Invalid native grid dimensions.";
            initial[axis++] = std::abs(float(double(scale)) - 1.f) <= 1.e-8f
                                  ? int(int64_t(size))
                                  : scaled_cells(int64_t(size), float(double(scale)), INT32_MAX);
        }
        metadata["initial"] = initial;
    } else {
        Variant max_dimension = control.get("max_dimension", Variant());
        if (!integer_value(max_dimension, 1, INT32_MAX))
            return "Missing RHI volume texture size limit.";
        metadata["max_dimension"] = max_dimension;
    }
    if (function == "GetNumCells" && grid && inputs == 1 && width == 3)
        return "";
    if (inputs == 4 && width == 1 &&
        ((grid && function == "SetNumCells") || (!grid && function == "SetRenderTargetSize")))
        return "";
    if (!grid || inputs != 1 || width != 1)
        return "Invalid native volume function signature: " + function;
    String type;
    if (function == "GetFloatAttributeIndex")
        type = "NiagaraFloat";
    else if (function == "GetVector2DAttributeIndex")
        type = "Vector2f";
    else if (function == "GetVectorAttributeIndex")
        type = "Vector3f";
    else if (function == "GetVector4AttributeIndex")
        type = "Vector4f";
    else
        return "Unsupported native grid function: " + function;
    String name = Dictionary(signature.get("specifiers", Dictionary())).get("Attribute", "");
    if (name.is_empty() || control.get("attributes", Variant()).get_type() != Variant::DICTIONARY)
        return "Missing native grid attribute layout.";
    Dictionary row = Dictionary(control["attributes"]).get(name, Dictionary());
    String row_type = row.get("type", "");
    int64_t offset = -1;
    if (row_type == type || (type == "Vector4f" && row_type == "LinearColor")) {
        if (!integer_value(row.get("offset", Variant()), 0, INT32_MAX))
            return "Invalid grid attribute offset.";
        offset = row["offset"];
    }
    metadata["offset"] = offset;
    return "";
}

void ExternalBinding::control_volume(const ExternalInputs &in, Dictionary &state,
                                     ExternalOutputs &out) const {
    if (kind == DebugBox || kind == Bounds) {
        if (!in[1])
            return;
        auto vector = [&](int n) { return Vector3(number(in[n]), number(in[n + 1]), number(in[n + 2])); };
        if (kind == DebugBox) {
            if (!bool(metadata["enabled"]))
                return;
            Array boxes = state.get("@debug_boxes", Array());
            Dictionary box;
            box["center"] = vector(2);
            box["rotation"] = Quaternion(number(in[5]), number(in[6]), number(in[7]), number(in[8]));
            box["extents"] = vector(9);
            box["color"] = Color(number(in[12]), number(in[13]), number(in[14]), number(in[15]));
            boxes.append(box);
            state["@debug_boxes"] = boxes;
        } else if (!String(metadata["emitter"]).is_empty()) {
            Dictionary all = state.get("@fixed_bounds", Dictionary()), box;
            Vector3 lo = vector(3), hi = vector(6);
            box["valid"] = in[2] != 0;
            box["min_cm"] = Array::make(lo.x, lo.y, lo.z);
            box["max_cm"] = Array::make(hi.x, hi.y, hi.z);
            all[metadata["emitter"]] = box;
            state["@fixed_bounds"] = all;
        }
        return;
    }
    if (metadata.has("offset")) {
        out[0] = uint32_t(int64_t(metadata["offset"]));
        return;
    }
    const String key = metadata["parameter"];
    if (function == "GetNumCells") {
        Vector3i cells = state.get(key, metadata["initial"]);
        for (int i = 0; i < 3; ++i)
            out[i] = cells[i];
        return;
    }
    const bool grid = function == "SetNumCells";
    Vector3i size;
    for (int i = 0; i < 3; ++i)
        size[i] = signed_word(in[i + 1]);
    bool ok = grid || (size.x > 0 && size.y > 0 && size.z > 0);
    out[0] = truth(ok);
    if (!ok)
        return;
    const float scale = double(metadata["scale"]);
    const int32_t maximum = int64_t(metadata["max_dimension"]);
    for (int i = 0; i < 3; ++i) {
        if (grid)
            size[i] = std::max(size[i], 1);
        if (!grid || std::abs(scale - 1.f) > 1.e-8f)
            size[i] = scaled_cells(size[i], scale, maximum);
    }
    state[key] = size;
}
} // namespace nbvm
