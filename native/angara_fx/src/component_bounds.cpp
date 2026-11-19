#include "component_bounds.h"
#include "particle_view.h"
#include <algorithm>
#include <cmath>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <limits>
namespace godot {
namespace {
constexpr const char *POLICY_PRODUCER =
    "NiagaraBridgeSourceLibrary.DescribeRendererOrder bounds policy";
constexpr const char *CVAR_PREFIX = "fx.Niagara.EmitterBounds.";
constexpr const char *SPRITE_CLASS = "NiagaraSpriteRendererProperties";
Basis conversion() {
  return Basis(Vector3(0, 0, -1), Vector3(1, 0, 0), Vector3(0, 1, 0));
}
Dictionary error_box(const String &message) {
  Dictionary result;
  result["error"] = message;
  return result;
}
Dictionary invalid_box() {
  Dictionary result;
  result["valid"] = false;
  return result;
}
// Script equality permits an int and float to describe the same schema number.
bool equal(const Variant &a, const Variant &b) {
  Variant result;
  bool valid = false;
  Variant::evaluate(Variant::OP_EQUAL, a, b, result, valid);
  return valid && bool(result);
}
} // namespace
double NBNativeComponentBounds::f32(double value) {
  return double(float(value));
}
bool NBNativeComponentBounds::number(const Variant &v) {
  return (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) &&
         std::isfinite(double(v));
}
bool NBNativeComponentBounds::sha(const Variant &v) {
  return v.get_type() == Variant::STRING && String(v).length() == 64 &&
         String(v).is_valid_hex_number(false);
}
String NBNativeComponentBounds::guid(const Variant &v) {
  if (v.get_type() != Variant::STRING)
    return String();
  String normalized = String(v).replace("-", "").to_upper();
  return normalized.length() == 32 && normalized.is_valid_hex_number(false) &&
                 normalized != String("0").repeat(32)
             ? normalized
             : String();
}
bool NBNativeComponentBounds::finite_transform(const Transform3D &v) {
  return v.origin.is_finite() && v.basis.get_column(0).is_finite() &&
         v.basis.get_column(1).is_finite() &&
         v.basis.get_column(2).is_finite() &&
         std::abs(double(v.basis.determinant())) > 1e-12;
}
bool NBNativeComponentBounds::valid_box(const Variant &v) {
  if (v.get_type() != Variant::DICTIONARY)
    return false;
  Dictionary b = v;
  if (b.get("valid", Variant()).get_type() != Variant::BOOL)
    return false;
  if (!bool(b["valid"]))
    return true;
  for (const char *key : {"min_cm", "max_cm"}) {
    Variant raw = b.get(key, Variant());
    if (raw.get_type() != Variant::ARRAY || Array(raw).size() != 3)
      return false;
    Array a = raw;
    for (int i = 0; i < 3; ++i)
      if (!number(a[i]))
        return false;
  }
  Array lo = b["min_cm"], hi = b["max_cm"];
  for (int i = 0; i < 3; ++i)
    if (double(lo[i]) > double(hi[i]))
      return false;
  return true;
}
Dictionary NBNativeComponentBounds::box(const Array &lo, const Array &hi) {
  Dictionary result;
  result["valid"] = true;
  result["min_cm"] = lo.duplicate();
  result["max_cm"] = hi.duplicate();
  return result;
}
Dictionary NBNativeComponentBounds::union_box(const Dictionary &left,
                                              const Dictionary &right) {
  if (!bool(left["valid"]))
    return right.duplicate(true);
  if (!bool(right["valid"]))
    return left.duplicate(true);
  Array la = left["min_cm"], lb = left["max_cm"], ra = right["min_cm"],
        rb = right["max_cm"], lo, hi;
  for (int i = 0; i < 3; ++i) {
    lo.append(std::min(double(la[i]), double(ra[i])));
    hi.append(std::max(double(lb[i]), double(rb[i])));
  }
  return box(lo, hi);
}
Dictionary NBNativeComponentBounds::center_extent(const Array &center,
                                                  const Array &extent,
                                                  double multiplier) {
  Array lo, hi;
  for (int i = 0; i < 3; ++i) {
    lo.append(double(center[i]) - double(extent[i]) * multiplier);
    hi.append(double(center[i]) + double(extent[i]) * multiplier);
  }
  return box(lo, hi);
}
Dictionary NBNativeComponentBounds::expand(const Dictionary &b,
                                           double multiplier) {
  Array lo = b["min_cm"], hi = b["max_cm"], center, extent;
  for (int i = 0; i < 3; ++i) {
    center.append((double(lo[i]) + double(hi[i])) * .5);
    extent.append((double(hi[i]) - double(lo[i])) * .5);
  }
  return center_extent(center, extent, multiplier);
}
bool NBNativeComponentBounds::contains(const Dictionary &outer,
                                       const Dictionary &inner) {
  Array lo = outer["min_cm"], hi = outer["max_cm"], a = inner["min_cm"],
        b = inner["max_cm"];
  for (int i = 0; i < 3; ++i)
    if (double(a[i]) < double(lo[i]) || double(b[i]) > double(hi[i]))
      return false;
  return true;
}
Array NBNativeComponentBounds::transform_vector(const Array &v,
                                                const Basis &basis) {
  Array result;
  for (int row = 0; row < 3; ++row) {
    double value = 0.;
    for (int column = 0; column < 3; ++column)
      value += double(basis.get_column(column)[row]) * double(v[column]);
    result.append(value);
  }
  return result;
}
Dictionary NBNativeComponentBounds::transform_box(const Dictionary &b,
                                                  const Basis &basis,
                                                  const Vector3 &translation) {
  Array lo = b["min_cm"], hi = b["max_cm"], center, extent, moved, scaled;
  for (int i = 0; i < 3; ++i) {
    center.append((double(lo[i]) + double(hi[i])) * .5);
    extent.append((double(hi[i]) - double(lo[i])) * .5);
  }
  for (int row = 0; row < 3; ++row) {
    double value = translation[row], radius = 0.;
    for (int column = 0; column < 3; ++column) {
      value += double(basis.get_column(column)[row]) * double(center[column]);
      radius += std::abs(double(basis.get_column(column)[row])) *
                double(extent[column]);
    }
    moved.append(value);
    scaled.append(radius);
  }
  return center_extent(moved, scaled, 1.);
}
Dictionary
NBNativeComponentBounds::world_cm_to_local_cm(const Dictionary &b,
                                              const Transform3D &owner) {
  Transform3D inverse = owner.affine_inverse();
  Basis convert = conversion();
  return transform_box(b, convert.transposed() * inverse.basis * convert,
                       convert.transposed().xform(inverse.origin) * 100.);
}
Dictionary NBNativeComponentBounds::to_world_godot(const Dictionary &b,
                                                   const Transform3D &owner) {
  return transform_box(b, owner.basis * conversion() * .01, owner.origin);
}
AABB NBNativeComponentBounds::aabb(const Dictionary &b) {
  Array lo = b["min_cm"], hi = b["max_cm"];
  Vector3 a(lo[0], lo[1], lo[2]), z(hi[0], hi[1], hi[2]);
  return AABB(a, z - a);
}
Dictionary NBNativeComponentBounds::failure(const Array &messages) {
  Dictionary result;
  result["verified"] = false;
  result["diagnostics"] = messages.duplicate();
  result["world_bounds_godot"] = AABB();
  return result;
}
Array NBNativeComponentBounds::calculators(const Dictionary &source) {
  if (source.has("bounds_calculators"))
    return source["bounds_calculators"];
  Array result;
  int64_t count = source["bounds_calculator_count"];
  for (int64_t i = 0; i < count; ++i) {
    Dictionary item;
    item["kind"] = "sprite";
    item["position_accessor_valid"] = source["position_accessor_valid"];
    item["sprite_size_accessor_valid"] = source["sprite_size_accessor_valid"];
    result.append(item);
  }
  return result;
}
Dictionary
NBNativeComponentBounds::simulation_bounds(const Dictionary &source,
                                           const Dictionary &snapshot,
                                           const Transform3D &owner) {
  if (snapshot.get("positions_cm", Variant()).get_type() != Variant::ARRAY ||
      snapshot.get("sprite_sizes_cm", Variant()).get_type() != Variant::ARRAY)
    return error_box(
        "CPU bounds require explicit source particle position/size arrays.");
  Array positions = snapshot["positions_cm"];
  if (positions.is_empty() || double(source["bounds_calculator_count"]) == 0.)
    return invalid_box();
  double inf = std::numeric_limits<double>::infinity();
  Array lo = Array::make(inf, inf, inf), hi = Array::make(-inf, -inf, -inf);
  for (int n = 0; n < positions.size(); ++n) {
    Variant raw = positions[n];
    if (raw.get_type() != Variant::VECTOR3 || !Vector3(raw).is_finite())
      return error_box("Invalid source particle position in bounds snapshot.");
    Vector3 p = raw;
    for (int i = 0; i < 3; ++i) {
      lo[i] = std::min(double(lo[i]), double(p[i]));
      hi[i] = std::max(double(hi[i]), double(p[i]));
    }
  }
  Dictionary b = invalid_box();
  Array list = calculators(source);
  for (int n = 0; n < list.size(); ++n) {
    Dictionary calculator = list[n];
    if (!bool(calculator["position_accessor_valid"]))
      continue;
    Dictionary current = calculator_box(calculator, snapshot, owner, lo, hi);
    if (current.has("error"))
      return current;
    b = union_box(b, current);
  }
  return b;
}
Dictionary NBNativeComponentBounds::calculator_box(const Dictionary &calculator,
                                                   const Dictionary &snapshot,
                                                   const Transform3D &owner,
                                                   const Array &minimum,
                                                   const Array &maximum) {
  Array lo = minimum.duplicate(), hi = maximum.duplicate(),
        positions = snapshot["positions_cm"];
  double size = f32(.0001), inf = std::numeric_limits<double>::infinity();
  String kind = calculator["kind"];
  if (kind == "mesh") {
    Array scale = Array::make(1., 1., 1.);
    if (bool(calculator["scale_accessor_valid"])) {
      Variant raw = snapshot.get("mesh_scales", Variant());
      if (raw.get_type() != Variant::ARRAY ||
          Array(raw).size() != positions.size())
        return error_box(
            "Position/Scale population mismatch in source bounds.");
      scale = Array::make(-inf, -inf, -inf);
      Array values = raw;
      for (int n = 0; n < values.size(); ++n) {
        if (values[n].get_type() != Variant::VECTOR3 ||
            !Vector3(values[n]).is_finite())
          return error_box("Invalid source Scale in bounds snapshot.");
        Vector3 value = values[n];
        for (int i = 0; i < 3; ++i)
          scale[i] = std::max(double(scale[i]), double(value[i]));
      }
    }
    Array extents = calculator["mesh_extents_cm"];
    double squared = 0.;
    for (int i = 0; i < 3; ++i)
      squared +=
          UtilityFunctions::pow(double(extents[i]) * double(scale[i]), 2.);
    size = std::max(size, f32(std::sqrt(squared)));
    Basis convert = conversion(),
          basis = convert.transposed() * owner.basis * convert;
    Array local = calculator["max_local_mesh_offset_cm"],
          world = calculator["max_world_mesh_offset_cm"];
    bool local_space = calculator["local_space"];
    Array moved = local_space ? transform_vector(world, basis.inverse())
                              : transform_vector(local, basis),
          fixed = local_space ? local : world;
    for (int i = 0; i < 3; ++i) {
      double offset =
          std::max(0., std::max(double(fixed[i]), double(moved[i])));
      lo[i] = double(lo[i]) - offset;
      hi[i] = double(hi[i]) + offset;
    }
  } else {
    double value = 50.;
    bool sprite = kind == "sprite";
    if (bool(calculator.get(sprite ? "sprite_size_accessor_valid"
                                   : "ribbon_width_accessor_valid",
                            false))) {
      Variant raw = snapshot.get(
          sprite ? "sprite_sizes_cm" : "ribbon_widths_cm", Variant());
      if (raw.get_type() != Variant::ARRAY ||
          Array(raw).size() != positions.size())
        return error_box("Position/size population mismatch in source bounds.");
      value = -inf;
      Array values = raw;
      for (int n = 0; n < values.size(); ++n) {
        Variant part = values[n];
        if (sprite) {
          if (part.get_type() != Variant::VECTOR2 || !Vector2(part).is_finite())
            return error_box("Invalid SpriteSize in source bounds.");
          Vector2 v = part;
          value = std::max(value, std::max(double(v.x), double(v.y)));
        } else {
          if (!number(part))
            return error_box("Invalid RibbonWidth in source bounds.");
          value = std::max(value, double(part));
        }
      }
    }
    size = std::max(size, std::abs(value) <= f32(.00000001) ? 1. : value);
  }
  for (int i = 0; i < 3; ++i) {
    lo[i] = double(lo[i]) - size;
    hi[i] = double(hi[i]) + size;
  }
  return box(lo, hi);
}
Dictionary NBNativeComponentBounds::authored_inputs(const Array &inputs,
                                                    const Array &keys) {
  Dictionary result;
  for (int n = 0; n < inputs.size(); ++n) {
    Ref<Resource> input = inputs[n];
    if (input.is_null() || !keys.has(input->get("input_name")))
      continue;
    Variant value = input->get("value");
    if (value.get_type() == Variant::ARRAY)
      value = Array(value).duplicate(true);
    else if (value.get_type() == Variant::DICTIONARY)
      value = Dictionary(value).duplicate(true);
    result[input->get("input_name")] = value;
  }
  return result;
}
Dictionary
NBNativeComponentBounds::authored_state(const Ref<Resource> &effect) {
  Dictionary state, items;
  if (effect.is_null())
    return state;
  state["system"] =
      authored_inputs(effect->get("settings"),
                      Array::make("FixedBounds", "bFixedBounds", "BoundsScale",
                                  "bOverrideTranslucencySortPriority",
                                  "TranslucencySortPriority",
                                  "bOverrideTranslucencySortDistanceOffset",
                                  "TranslucencySortDistanceOffset"));
  state["emitters"] = items;
  Array emitters = effect->get("emitters");
  for (int n = 0; n < emitters.size(); ++n) {
    Ref<Resource> emitter = emitters[n];
    Dictionary item, renderers;
    item["enabled"] = emitter->get("enabled");
    item["settings"] =
        authored_inputs(emitter->get("settings"),
                        Array::make("LocalSpace", "SimTarget",
                                    "CalculateBoundsMode", "FixedBounds"));
    item["renderers"] = renderers;
    Array sources = emitter->get("renderers");
    for (int i = 0; i < sources.size(); ++i) {
      Ref<Resource> source = sources[i];
      Dictionary renderer;
      renderer["enabled"] = source->get("enabled");
      renderer["type"] = source->get("renderer_type");
      renderer["bindings"] = Array(source->get("bindings")).duplicate(true);
      renderer["properties"] =
          authored_inputs(source->get("properties"),
                          Array::make("bIsEnabled", "SourceMode",
                                      "PositionBinding", "SpriteSizeBinding"));
      if (String(source->get("renderer_type")) == "MeshRenderer") {
        Array meshes, entries = source->get("mesh_entries");
        for (int j = 0; j < entries.size(); ++j) {
          Ref<Resource> entry = entries[j];
          Dictionary mesh;
          for (const char *key :
               {"source_path", "user_parameter", "local_transform",
                "pivot_offset", "pivot_space", "lod_index"})
            mesh[key] = entry->get(key);
          meshes.append(mesh);
        }
        renderer["meshes"] = meshes;
      }
      renderers[source->get("stable_id")] = renderer;
    }
    items[emitter->get("stable_id")] = item;
  }
  return state;
}
Dictionary NBNativeComponentBounds::collect_snapshots(const Array &emitters) {
  return collect_for_policy(emitters, nullptr);
}
Dictionary NBNativeComponentBounds::collect_for_policy(const Array &emitters, const NBNativeComponentBounds *bounds) {
  TypedArray<Dictionary> snapshots;
  bool any = false;
  for (int n = 0; n < emitters.size(); ++n) {
    Dictionary emitter = emitters[n], plan = emitter["plan"], snapshot;
    bool completed = emitter.get("bounds_post_tick_completed", false);
    any = any || completed;
    Ref<Resource> source = plan["source"];
    Variant id = source->get("stable_id");
    snapshot["emitter_stable_id"] = id;
    // Public snapshot callers receive every lane. The native history step only
    // consumes particle columns for completed, dynamic CPU emitters.
    bool needs_particles = true;
    if (bounds) {
      Dictionary entry = bounds->emitters.get(id, Dictionary());
      needs_particles = completed && !entry.is_empty() && String(entry["simulation_target"]) != "GPUComputeSim" &&
                        !bool(bounds->fixed_bounds(entry)["valid"]);
    }
    TypedArray<Vector3> positions, scales;
    TypedArray<Vector2> sizes;
    TypedArray<double> widths;
    if (needs_particles) {
      if(bool(emitter.get("gpu_births_only",false)) && !Array(emitter["particles"]).is_empty())
        NBNativeParticleView::materialize(emitter);
      Array particles = emitter["particles"];
      Ref<NBNativeParticleView> view = NBNativeParticleView::for_emitter(emitter);
      if (view.is_valid() && !view->get_errors().is_empty()) view.unref();
      int64_t position = -1, size = -1, scale = -1, width = -1;
      if (view.is_valid()) {
        position = view->field_handle("position"); size = view->field_handle("size");
        scale = view->field_handle("mesh_scale"); width = view->field_handle("ribbon_width");
      }
      for (int64_t i = 0, count=view.is_valid() ? view->get_count() : particles.size(); i < count; ++i) {
        if (view.is_valid()) {
          positions.append(view->read(position,i)); sizes.append(view->read(size,i));
          scales.append(view->read(scale,i)); widths.append(view->read(width,i));
        } else {
          Dictionary p = particles[i];
          positions.append(p["position"]); sizes.append(p["size"]);
          scales.append(p["mesh_scale"]); widths.append(p["ribbon_width"]);
        }
      }
    }
    snapshot["positions_cm"] = positions;
    snapshot["sprite_sizes_cm"] = sizes;
    snapshot["mesh_scales"] = scales;
    snapshot["ribbon_widths_cm"] = widths;
    snapshot["local_space"] = plan["local_space"];
    snapshot["backend"] =
        String(plan["backend"]) == "gpu_compute" ? "gpu" : "cpu";
    snapshot["post_tick_completed"] = completed;
    snapshots.append(snapshot);
  }
  Dictionary result;
  result["snapshots"] = snapshots;
  result["any_post_tick"] = any;
  return result;
}
String NBNativeComponentBounds::check_calculators(const Dictionary &item) {
  Variant count = item.get("bounds_calculator_count", Variant());
  if (!number(count) || double(count) < 0. ||
      double(count) != std::floor(double(count)))
    return "Missing native bounds calculator count.";
  if (item.has("bounds_calculators")) {
    Variant raw = item["bounds_calculators"];
    if (!equal(item.get("bounds_calculators_producer", Variant()),
               "Renderer.CreateBoundsCalculator with current compiled dataset "
               "accessors") ||
        !equal(item.get("bounds_calculators_count_matches_cached", Variant()),
               true) ||
        raw.get_type() != Variant::ARRAY || Array(raw).size() != double(count))
      return "Incomplete native bounds calculator inventory.";
    Array list = raw, indices;
    for (int n = 0; n < list.size(); ++n) {
      Variant raw_calculator = list[n];
      if (raw_calculator.get_type() != Variant::DICTIONARY)
        return "Unsupported native bounds calculator.";
      Dictionary calculator = raw_calculator;
      if (!equal(calculator.get("supported", Variant()), true) ||
          calculator.get("position_accessor_valid", Variant()).get_type() !=
              Variant::BOOL)
        return "Unsupported native bounds calculator.";
      Variant index = calculator.get("renderer_index", Variant());
      if (!number(index) || double(index) < 0. ||
          double(index) != std::floor(double(index)) || indices.has(index))
        return "Invalid/duplicate bounds renderer index.";
      indices.append(index);
      String kind = UtilityFunctions::str(calculator.get("kind", "")), expected;
      if (kind == "sprite")
        expected = SPRITE_CLASS;
      else if (kind == "mesh")
        expected = "NiagaraMeshRendererProperties";
      else if (kind == "ribbon")
        expected = "NiagaraRibbonRendererProperties";
      if (expected.is_empty() ||
          !equal(calculator.get("renderer_class", Variant()), expected))
        return "Bounds calculator class/kind differs.";
      String field = kind == "sprite" ? "sprite_size"
                     : kind == "mesh" ? "scale"
                                      : "ribbon_width";
      if (calculator.get(field + "_accessor_valid", Variant()).get_type() !=
              Variant::BOOL ||
          calculator.get(field + "_storage_half", Variant()).get_type() !=
              Variant::BOOL)
        return "Missing native bounds size accessor.";
      if (bool(calculator[field + "_storage_half"]))
        return "Half-precision bounds reduction is not implemented.";
      if (kind == "mesh") {
        if (!equal(calculator.get("local_space", Variant()),
                   item["local_space"]))
          return "Mesh bounds local-space mode differs.";
        for (const char *key : {"mesh_extents_cm", "max_local_mesh_offset_cm",
                                "max_world_mesh_offset_cm"}) {
          Variant values = calculator.get(key, Variant());
          if (values.get_type() != Variant::ARRAY || Array(values).size() != 3)
            return "Invalid native mesh bounds vector.";
          Array parts = values;
          for (int i = 0; i < 3; ++i)
            if (!number(parts[i]) || double(parts[i]) < 0.)
              return "Invalid native mesh bounds vector.";
        }
      }
    }
    return String();
  }
  if (double(count) == 0.)
    return String();
  Variant raw = item.get("enabled_renderer_classes", Variant());
  if (raw.get_type() != Variant::ARRAY || Array(raw).size() != double(count))
    return "CPU dynamic bounds include unsupported or unidentified renderer "
           "calculators.";
  Array classes = raw;
  for (int n = 0; n < classes.size(); ++n)
    if (!equal(classes[n], SPRITE_CLASS))
      return "CPU dynamic bounds include unsupported or unidentified renderer "
             "calculators.";
  for (const char *flag :
       {"position_accessor_valid", "sprite_size_accessor_valid",
        "sprite_size_storage_half"})
    if (item.get(flag, Variant()).get_type() != Variant::BOOL)
      return String("Missing native compiled bounds accessor: ") + flag;
  return bool(item["sprite_size_storage_half"])
             ? "Half-precision SpriteSize bounds reduction is not implemented."
             : String();
}
NBNativeComponentBounds::NBNativeComponentBounds() { reset(); }
void NBNativeComponentBounds::reset() {
  cached = Dictionary();
  system_local = box(Array::make(0., 0., 0.), Array::make(0., 0., 0.));
  if (bool(contract.get("system_fixed_bounds_enabled", false)))
    system_local =
        Dictionary(contract["system_fixed_bounds_local"]).duplicate(true);
  component_local = invalid_box();
  force_time = 0.;
}
Dictionary NBNativeComponentBounds::configuration_error(const String &message) {
  errors.clear();
  errors.append(message);
  return failure(errors);
}
Dictionary NBNativeComponentBounds::configure(const Dictionary &supplied) {
  contract = Dictionary();
  policy = Dictionary();
  emitters = Dictionary();
  errors = TypedArray<String>();
  reset();
  if (!equal(supplied.get("verified", Variant()), true) ||
      !sha(supplied.get("source_package_sha256", Variant())))
    return configuration_error(
        "Bounds require verified package-bound source policy.");
  Variant raw = supplied.get("bounds_policy", Variant());
  if (raw.get_type() != Variant::DICTIONARY)
    return configuration_error("Missing complete native bounds policy.");
  Dictionary p = raw;
  if (!equal(p.get("schema_version", Variant()), 1) ||
      !equal(p.get("producer", Variant()), POLICY_PRODUCER) ||
      !equal(p.get("complete", Variant()), true))
    return configuration_error("Missing complete native bounds policy.");
  if (!equal(p.get("observation_scope", Variant()),
             "fresh_component_after_SetAsset_current_process_policy") ||
      !equal(p.get("auto_manage_attachment", Variant()), false))
    return configuration_error(
        "Actor overrides, automatic attachment and registered-component state "
        "need a separate scene contract.");
  for (const char *field : {"component_bounds_scale",
                            "max_time_before_force_update_transform_seconds"})
    if (!number(p.get(field, Variant())) || double(p[field]) < 0.)
      return configuration_error(
          String("Missing/invalid native component policy: ") + field);
  if (supplied.get("system_fixed_bounds_enabled", Variant()).get_type() !=
          Variant::BOOL ||
      !valid_box(supplied.get("system_fixed_bounds_local", Variant())))
    return configuration_error("Missing native system fixed-bounds flag/box.");
  Dictionary fixed_system_box = supplied["system_fixed_bounds_local"];
  if (bool(supplied["system_fixed_bounds_enabled"]) &&
      !bool(fixed_system_box["valid"]))
    return configuration_error("Enabled system fixed bounds are invalid.");
  for (const char *field :
       {"component_system_fixed_bounds_local", "gpu_dynamic_fallback_local"})
    if (!valid_box(p.get(field, Variant())))
      return configuration_error(String("Missing native bounds policy box: ") +
                                 field);
  if (!bool(Dictionary(p["gpu_dynamic_fallback_local"])["valid"]))
    return configuration_error("Native GPU dynamic fallback box is invalid.");
  if (p.get("cvars", Variant()).get_type() != Variant::DICTIONARY)
    return configuration_error("Bounds CVar observations are absent.");
  Dictionary cvars = p["cvars"];
  for (const char *key : {"DynamicSnapValue", "DynamicExpandMultiplier",
                          "FixedExpandMultiplier"}) {
    Variant value = cvars.get(String(CVAR_PREFIX) + key, Variant());
    bool valid = value.get_type() == Variant::DICTIONARY;
    if (valid) {
      Dictionary v = value;
      valid = number(v.get("value", Variant())) &&
              number(v.get("flags", Variant())) && double(v["value"]) >= 0.;
    }
    if (!valid)
      return configuration_error(
          String("Missing/invalid measured bounds CVar: ") + key);
  }
  if (p.get("emitters", Variant()).get_type() != Variant::ARRAY ||
      supplied.get("emitter_bindings", Variant()).get_type() != Variant::ARRAY)
    return configuration_error("Bounds require native emitter inventory and "
                               "explicit stable-ID/GUID bindings.");
  Dictionary by_guid;
  Array list = p["emitters"];
  for (int n = 0; n < list.size(); ++n) {
    Variant raw_item = list[n];
    if (raw_item.get_type() != Variant::DICTIONARY ||
        Dictionary(raw_item).get("enabled", Variant()).get_type() !=
            Variant::BOOL)
      return configuration_error("Malformed bounds emitter inventory.");
    Dictionary item = raw_item;
    String id = guid(item.get("emitter_handle_guid", Variant()));
    if (id.is_empty() || by_guid.has(id))
      return configuration_error(
          "Duplicate/missing native bounds emitter GUID.");
    by_guid[id] = item;
  }
  Dictionary bound;
  bool fixed_system =
      bool(Dictionary(p["component_system_fixed_bounds_local"])["valid"]) ||
      bool(supplied["system_fixed_bounds_enabled"]);
  Array bindings = supplied["emitter_bindings"];
  for (int n = 0; n < bindings.size(); ++n) {
    Variant raw_binding = bindings[n];
    if (raw_binding.get_type() != Variant::DICTIONARY ||
        Dictionary(raw_binding)
                .get("emitter_stable_id", Variant())
                .get_type() != Variant::STRING ||
        String(Dictionary(raw_binding)["emitter_stable_id"]).is_empty())
      return configuration_error("Malformed bounds stable-ID binding.");
    Dictionary binding = raw_binding;
    String id = guid(binding.get("emitter_handle_guid", Variant())),
           stable = binding["emitter_stable_id"];
    if (!by_guid.has(id) || bound.has(id) || emitters.has(stable))
      return configuration_error("Missing/duplicate native bounds binding.");
    Dictionary item = by_guid[id];
    bound[id] = true;
    if (!bool(item["enabled"]))
      continue;
    String target = item.get("simulation_target", "");
    if (!equal(item.get("stateful", Variant()), true) ||
        item.get("local_space", Variant()).get_type() != Variant::BOOL ||
        (target != "CPUSim" && target != "GPUComputeSim"))
      return configuration_error(
          "Stateless or unknown bounds emitter semantics are unsupported.");
    for (const char *field : {"component_override_fixed_bounds_local",
                              "authored_fixed_bounds_local"})
      if (!valid_box(item.get(field, Variant())))
        return configuration_error(String("Missing native emitter bounds: ") +
                                   field);
    if (!fixed_system &&
        !bool(Dictionary(
            item["component_override_fixed_bounds_local"])["valid"])) {
      String mode = item.get("calculate_bounds_mode", "");
      if (mode == "Fixed") {
        if (!bool(Dictionary(item["authored_fixed_bounds_local"])["valid"]))
          return configuration_error("Authored fixed bounds are invalid.");
      } else if (mode == "Dynamic") {
        if (target == "CPUSim") {
          String diagnostic = check_calculators(item);
          if (!diagnostic.is_empty())
            return configuration_error(diagnostic);
        }
      } else
        return configuration_error("Unimplemented emitter bounds mode: " +
                                   UtilityFunctions::str(item.get(
                                       "calculate_bounds_mode", Variant())));
    }
    emitters[stable] = item.duplicate(true);
  }
  Array ids = by_guid.keys();
  for (int n = 0; n < ids.size(); ++n)
    if (bool(Dictionary(by_guid[ids[n]])["enabled"]) && !bound.has(ids[n]))
      return configuration_error(
          "An enabled native emitter is missing from bounds bindings.");
  contract = supplied.duplicate(true);
  policy = p.duplicate(true);
  reset();
  Dictionary result;
  result["verified"] = true;
  result["diagnostics"] = Array();
  return result;
}
Dictionary NBNativeComponentBounds::fixed_bounds(const Dictionary &source) const {
  Dictionary fixed = runtime_fixed_bounds.get(source.get("emitter_name",""), source["component_override_fixed_bounds_local"]);
  if (!bool(fixed["valid"]))
    fixed = policy["component_system_fixed_bounds_local"];
  if (!bool(fixed["valid"]) && bool(contract["system_fixed_bounds_enabled"]))
    fixed = contract["system_fixed_bounds_local"];
  if (!bool(fixed["valid"]) &&
      String(source["calculate_bounds_mode"]) == "Fixed")
    fixed = source["authored_fixed_bounds_local"];
  return fixed;
}
Dictionary
NBNativeComponentBounds::emitter_bounds(const Dictionary &source,
                                        const Dictionary &snapshot,
                                        const Transform3D &owner) const {
  Dictionary fixed = fixed_bounds(source);
  Dictionary result;
  if (bool(fixed["valid"])) {
    result["dynamic"] = false;
    result["box"] = fixed.duplicate(true);
    return result;
  }
  if (String(source["simulation_target"]) == "GPUComputeSim") {
    result["dynamic"] = false;
    result["box"] =
        Dictionary(policy["gpu_dynamic_fallback_local"]).duplicate(true);
    return result;
  }
  Dictionary b = simulation_bounds(source, snapshot, owner);
  if (b.has("error"))
    return b;
  result["dynamic"] = bool(b["valid"]);
  if (bool(b["valid"]) && !bool(source["local_space"]))
    b = world_cm_to_local_cm(b, owner);
  result["box"] = b;
  return result;
}
Dictionary NBNativeComponentBounds::aggregate(const Dictionary &cache) const {
  Dictionary fixed_component = policy["component_system_fixed_bounds_local"];
  if (bool(fixed_component["valid"]))
    return fixed_component.duplicate(true);
  if (bool(contract["system_fixed_bounds_enabled"]))
    return Dictionary(contract["system_fixed_bounds_local"]).duplicate(true);
  Dictionary dynamic = invalid_box(), fixed = invalid_box();
  Array values = cache.values();
  for (int n = 0; n < values.size(); ++n) {
    Dictionary entry = values[n];
    if (bool(entry["dynamic"]))
      dynamic = union_box(dynamic, entry["box"]);
    else
      fixed = union_box(fixed, entry["box"]);
  }
  Dictionary cvars = policy["cvars"];
  if (bool(dynamic["valid"])) {
    double snap =
        Dictionary(cvars[String(CVAR_PREFIX) + "DynamicSnapValue"])["value"];
    Array lo = dynamic["min_cm"], hi = dynamic["max_cm"], center, extent;
    for (int i = 0; i < 3; ++i) {
      center.append((double(lo[i]) + double(hi[i])) * .5);
      double radius = (double(hi[i]) - double(lo[i])) * .5;
      extent.append(snap > 1. ? std::ceil(radius / snap) * snap : radius);
    }
    dynamic = center_extent(
        center, extent,
        Dictionary(
            cvars[String(CVAR_PREFIX) + "DynamicExpandMultiplier"])["value"]);
  }
  if (bool(fixed["valid"]))
    fixed = expand(
        fixed,
        Dictionary(
            cvars[String(CVAR_PREFIX) + "FixedExpandMultiplier"])["value"]);
  Dictionary combined = union_box(dynamic, fixed);
  return bool(combined["valid"])
             ? combined
             : box(Array::make(-1., -1., -1.), Array::make(1., 1., 1.));
}
Dictionary NBNativeComponentBounds::step(const Array &snapshots,
                                         const Transform3D &owner, double delta,
                                         bool system_tick,
                                         bool component_tick) {
  if (contract.is_empty())
    return failure(errors.is_empty()
                       ? Array::make("Native bounds are not configured.")
                       : Array(errors));
  if (!std::isfinite(delta) || delta < 0. || !finite_transform(owner))
    return failure(Array::make(
        "Non-finite bounds clock or singular/non-finite owner transform."));
  Dictionary cache = cached.duplicate(true),
             local = system_local.duplicate(true);
  if (system_tick) {
    Dictionary inputs;
    for (int n = 0; n < snapshots.size(); ++n) {
      Variant raw = snapshots[n];
      if (raw.get_type() != Variant::DICTIONARY)
        return failure(Array::make("Bounds snapshot lacks stable identity or "
                                   "explicit emitter PostTick status."));
      Dictionary snapshot = raw;
      if (snapshot.get("emitter_stable_id", Variant()).get_type() !=
              Variant::STRING ||
          snapshot.get("post_tick_completed", Variant()).get_type() !=
              Variant::BOOL)
        return failure(Array::make("Bounds snapshot lacks stable identity or "
                                   "explicit emitter PostTick status."));
      String id = snapshot["emitter_stable_id"];
      if (!emitters.has(id) || inputs.has(id))
        return failure(
            Array::make("Unknown/duplicate emitter in bounds snapshot."));
      inputs[id] = snapshot;
    }
    if (inputs.size() != emitters.size())
      return failure(
          Array::make("Bounds snapshots must cover every enabled source "
                      "emitter, including non-rendered/empty emitters."));
    Array ids = emitters.keys();
    for (int n = 0; n < ids.size(); ++n) {
      Variant id = ids[n];
      Dictionary snapshot = inputs[id], source = emitters[id];
      if (snapshot.has("reset_bounds")) {
        if (snapshot["reset_bounds"].get_type() != Variant::BOOL)
          return failure(Array::make("Invalid emitter bounds reset flag."));
        if (bool(snapshot["reset_bounds"]))
          cache.erase(id);
      }
      if (!bool(snapshot["post_tick_completed"]))
        continue;
      if (!equal(snapshot.get("local_space", Variant()), source["local_space"]))
        return failure(Array::make(
            "Local-space edit differs from the verified bounds source."));
      Dictionary calculated = emitter_bounds(source, snapshot, owner);
      if (calculated.has("error"))
        return failure(Array::make(calculated["error"]));
      cache[id] = calculated;
    }
    local = aggregate(cache);
  }
  Dictionary current = component_local.duplicate(true);
  double timer = force_time;
  bool updated = false;
  if (component_tick) {
    if (!bool(local.get("valid", false)))
      return failure(
          Array::make("Component PostTick has no established system bounds; "
                      "supply actual initialization/tick chronology."));
    timer = f32(timer + f32(delta));
    if (!bool(current["valid"]) || !contains(current, local) ||
        timer >
            double(policy["max_time_before_force_update_transform_seconds"])) {
      current = local.duplicate(true);
      timer = 0.;
      updated = true;
    }
  }
  Dictionary displayed = current;
  if (bool(current["valid"]))
    displayed = expand(current, policy["component_bounds_scale"]);
  else if (bool(Dictionary(
               policy["component_system_fixed_bounds_local"])["valid"]))
    displayed = policy["component_system_fixed_bounds_local"];
  else if (bool(contract["system_fixed_bounds_enabled"]))
    displayed = contract["system_fixed_bounds_local"];
  else
    displayed = box(Array::make(-1., -1., -1.), Array::make(1., 1., 1.));
  Dictionary world = to_world_godot(displayed, owner);
  cached = cache;
  system_local = local;
  component_local = current;
  force_time = timer;
  Dictionary result;
  result["verified"] = true;
  result["diagnostics"] = Array();
  result["world_bounds_godot"] = aabb(world);
  result["primitive_local_bounds_cm"] = displayed.duplicate(true);
  result["system_local_bounds_cm"] = local.duplicate(true);
  result["component_local_bounds_cm"] = current.duplicate(true);
  result["component_bounds_updated"] = updated;
  result["force_update_time_seconds"] = timer;
  result["emitter_bounds"] = cache.duplicate(true);
  result["precision_limit"] =
      "Godot owner transform and final AABB use engine float32; intermediate "
      "UE box scalars use float64.";
  return result;
}
void NBNativeComponentBounds::_bind_methods() {
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("authored_state", "effect"),
                              &NBNativeComponentBounds::authored_state);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_authored_inputs", "inputs", "keys"),
                              &NBNativeComponentBounds::authored_inputs);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("collect_snapshots", "emitters"),
                              &NBNativeComponentBounds::collect_snapshots);
  ClassDB::bind_static_method(
      "NBNativeComponentBounds",
      D_METHOD("_simulation_bounds", "source", "snapshot", "owner"),
      &NBNativeComponentBounds::simulation_bounds);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_calculators", "source"),
                              &NBNativeComponentBounds::calculators);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_calculator_box", "calculator",
                                       "snapshot", "owner", "minimum",
                                       "maximum"),
                              &NBNativeComponentBounds::calculator_box);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_check_calculators", "item"),
                              &NBNativeComponentBounds::check_calculators);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_box", "minimum", "maximum"),
                              &NBNativeComponentBounds::box);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_union", "left", "right"),
                              &NBNativeComponentBounds::union_box);
  ClassDB::bind_static_method(
      "NBNativeComponentBounds",
      D_METHOD("_center_extent", "center", "extent", "multiplier"),
      &NBNativeComponentBounds::center_extent);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_expand", "box", "multiplier"),
                              &NBNativeComponentBounds::expand);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_contains", "outer", "inner"),
                              &NBNativeComponentBounds::contains);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_transform_vector", "vector", "basis"),
                              &NBNativeComponentBounds::transform_vector);
  ClassDB::bind_static_method(
      "NBNativeComponentBounds",
      D_METHOD("_transform_box", "box", "basis", "translation"),
      &NBNativeComponentBounds::transform_box);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_world_cm_to_local_cm", "box", "owner"),
                              &NBNativeComponentBounds::world_cm_to_local_cm);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_to_world_godot", "box", "owner"),
                              &NBNativeComponentBounds::to_world_godot);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_aabb", "box"),
                              &NBNativeComponentBounds::aabb);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_valid_box", "value"),
                              &NBNativeComponentBounds::valid_box);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_number", "value"),
                              &NBNativeComponentBounds::number);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_sha", "value"),
                              &NBNativeComponentBounds::sha);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_guid", "value"),
                              &NBNativeComponentBounds::guid);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_finite_transform", "value"),
                              &NBNativeComponentBounds::finite_transform);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_f32", "value"),
                              &NBNativeComponentBounds::f32);
  ClassDB::bind_static_method("NBNativeComponentBounds",
                              D_METHOD("_failure", "messages"),
                              &NBNativeComponentBounds::failure);
  ClassDB::bind_method(D_METHOD("configure", "contract"),
                       &NBNativeComponentBounds::configure);
  ClassDB::bind_method(D_METHOD("reset"), &NBNativeComponentBounds::reset);
  ClassDB::bind_method(D_METHOD("step", "snapshots", "owner", "delta",
                                "system_tick", "component_tick"),
                       &NBNativeComponentBounds::step);
}
} // namespace godot
