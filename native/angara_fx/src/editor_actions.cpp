#include "editor_actions.h"
#include "document_copy.h"
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
namespace godot {
using nbdocument::dict;
namespace {
Variant optional(const Ref<Resource> &resource) {
  return resource.is_valid() ? Variant(resource) : Variant();
}
String identity() {
  return (Variant(Time::get_singleton()->get_unix_time_from_system())
              .stringify() +
          String(":") +
          Variant(Time::get_singleton()->get_ticks_usec()).stringify() +
          String(":") + Variant(UtilityFunctions::randi()).stringify())
      .sha256_text()
      .substr(0, 20);
}
Variant replace_path(Variant current, const Array &path, int64_t depth,
                     const Variant &value) {
  if (depth == path.size())
    return value;
  bool valid = false;
  Variant child = current.get(path[depth], &valid);
  if (!valid)
    return current;
  current.set(path[depth], replace_path(child, path, depth + 1, value), &valid);
  return current;
}
} // namespace
TypedArray<Dictionary>
NBNativeEditorActions::catalog(const TypedArray<Dictionary> &entries,
                               const String &stage) {
  TypedArray<Dictionary> result;
  for (int64_t i = 0; i < entries.size(); ++i) {
    Dictionary entry = entries[i];
    if (!Array(entry["stages"]).has(stage))
      continue;
    Dictionary descriptor = entry.duplicate(true);
    descriptor["cpu_status"] = "prototype";
    descriptor["gpu_status"] = "unimplemented";
    result.append(descriptor);
  }
  return result;
}
Ref<Resource> NBNativeEditorActions::create_module(
    const TypedArray<Dictionary> &entries, const String &name,
    const Ref<Script> &module_schema, const Ref<Script> &input_schema) {
  for (int64_t i = 0; i < entries.size(); ++i) {
    Dictionary entry = entries[i];
    if (entry["name"] != Variant(name))
      continue;
    Ref<Resource> module = module_schema->call("new");
    module->set("module_name", name);
    module->set("stable_id", identity());
    Array defaults = entry["defaults"], inputs = module->get("inputs");
    for (int64_t j = 0; j < defaults.size(); ++j) {
      Dictionary argument = defaults[j];
      Ref<Resource> input = input_schema->call("new");
      input->set("input_name", argument["name"]);
      Variant value = argument["value"];
      if (value.get_type() == Variant::DICTIONARY)
        value = Dictionary(value).duplicate(true);
      else if (value.get_type() == Variant::ARRAY)
        value = Array(value).duplicate(true);
      input->set("value", value);
      inputs.append(input);
    }
    return module;
  }
  return Ref<Resource>();
}
String NBNativeEditorActions::validate_module_stage(
    const TypedArray<Dictionary> &entries, const Ref<Resource> &module,
    const String &stage, const String &original) {
  if (module.is_null())
    return "No module selected.";
  String name = module->get("module_name");
  if (module->get("operation_kind") == Variant("assignment")) {
    if (name == "Particles.SpriteAlignment" &&
        (stage == "ParticleSpawn" || stage == "ParticleUpdate"))
      return "";
    return stage == original ? String()
                             : String("Assignment stage is unknown; keep it in "
                                      "its original stage.");
  }
  for (int64_t i = 0; i < entries.size(); ++i) {
    Dictionary descriptor = entries[i];
    if (descriptor["name"] == Variant(name))
      return Array(descriptor["stages"]).has(stage)
                 ? String()
                 : name + String(" is not valid in ") + stage + String(".");
  }
  return stage == original
             ? String()
             : String(
                   "Module stage is unknown; keep it in its original stage.");
}
Dictionary NBNativeEditorActions::capture_selection(
    const TypedArray<Resource> &selected, const Ref<Resource> &primary,
    const Ref<Resource> &stage, const Ref<Resource> &module) {
  TypedArray<String> ids;
  for (int64_t i = 0; i < selected.size(); ++i) {
    Ref<Resource> emitter = selected[i];
    if (emitter.is_valid())
      ids.append(emitter->get("stable_id"));
  }
  return dict(
      {{"emitters", ids},
       {"primary",
        primary.is_valid() ? primary->get("stable_id") : Variant("")},
       {"stage", stage.is_valid() ? stage->get("stable_id") : Variant("")},
       {"module", module.is_valid() ? module->get("stable_id") : Variant("")}});
}
Dictionary
NBNativeEditorActions::resolve_selection(const Ref<Resource> &effect,
                                         const Dictionary &selection) {
  TypedArray<Resource> selected;
  Ref<Resource> primary, stage, module;
  if (effect.is_valid()) {
    Array emitters = effect->get("emitters"),
          ids = selection.get("emitters", Array());
    for (int64_t i = 0; i < emitters.size(); ++i) {
      Ref<Resource> emitter = emitters[i];
      if (ids.has(emitter->get("stable_id")))
        selected.append(emitter);
      if (emitter->get("stable_id") == selection.get("primary", ""))
        primary = emitter;
    }
    Ref<Resource> owner = primary.is_valid() ? primary : effect;
    Array stages = owner->get("stages");
    for (int64_t i = 0; i < stages.size(); ++i) {
      Ref<Resource> candidate = stages[i];
      if (candidate->get("stable_id") == selection.get("stage", ""))
        stage = candidate;
      Array operations = candidate->get("operations");
      for (int64_t j = 0; j < operations.size(); ++j) {
        Ref<Resource> operation = operations[j];
        if (operation->get("stable_id") == selection.get("module", ""))
          module = operation;
      }
    }
  }
  return dict({{"emitters", selected},
               {"primary", optional(primary)},
               {"stage", optional(stage)},
               {"module", optional(module)}});
}
Dictionary
NBNativeEditorActions::validate_selection(const Ref<Resource> &effect,
                                          const Dictionary &selection) {
  TypedArray<Resource> selected;
  Ref<Resource> primary = selection.get("primary", Variant()),
                stage = selection.get("stage", Variant()),
                module = selection.get("module", Variant()),
                renderer = selection.get("renderer", Variant());
  if (effect.is_null())
    return dict({{"emitters", selected},
                 {"primary", Variant()},
                 {"stage", Variant()},
                 {"module", Variant()},
                 {"renderer", optional(renderer)}});
  Array emitters = effect->get("emitters"),
        previous = selection.get("emitters", Array());
  for (int64_t i = 0; i < previous.size(); ++i)
    if (emitters.has(previous[i]))
      selected.append(previous[i]);
  if (primary.is_valid() && !emitters.has(primary))
    primary =
        selected.is_empty() ? Ref<Resource>() : Ref<Resource>(selected.back());
  Ref<Resource> owner = primary.is_valid() ? primary : effect;
  if (stage.is_valid() && !Array(owner->get("stages")).has(stage))
    stage.unref();
  if (module.is_valid() && selected_module_stage(owner, module).is_null())
    module.unref();
  if (renderer.is_valid() &&
      (primary.is_null() || !Array(primary->get("renderers")).has(renderer)))
    renderer.unref();
  return dict({{"emitters", selected},
               {"primary", optional(primary)},
               {"stage", optional(stage)},
               {"module", optional(module)},
               {"renderer", optional(renderer)}});
}
Ref<Resource>
NBNativeEditorActions::owner_of_stage(const Ref<Resource> &effect,
                                      const Ref<Resource> &stage) {
  if (effect.is_null() || stage.is_null())
    return Ref<Resource>();
  if (Array(effect->get("stages")).has(stage))
    return effect;
  Array emitters = effect->get("emitters");
  for (int64_t i = 0; i < emitters.size(); ++i) {
    Ref<Resource> emitter = emitters[i];
    if (Array(emitter->get("stages")).has(stage))
      return emitter;
  }
  return Ref<Resource>();
}
Ref<Resource>
NBNativeEditorActions::selected_module_stage(const Ref<Resource> &owner,
                                             const Ref<Resource> &module) {
  if (owner.is_null() || module.is_null())
    return Ref<Resource>();
  Array stages = owner->get("stages");
  for (int64_t i = 0; i < stages.size(); ++i) {
    Ref<Resource> stage = stages[i];
    if (Array(stage->get("operations")).has(module))
      return stage;
  }
  return Ref<Resource>();
}
bool NBNativeEditorActions::contains_renderer(const Ref<Resource> &effect,
                                              const Ref<Resource> &renderer) {
  if (effect.is_null() || renderer.is_null())
    return false;
  Array emitters = effect->get("emitters");
  for (int64_t i = 0; i < emitters.size(); ++i) {
    Ref<Resource> emitter = emitters[i];
    if (Array(emitter->get("renderers")).has(renderer))
      return true;
  }
  return false;
}
int64_t
NBNativeEditorActions::material_slot_count(const Ref<Resource> &renderer) {
  int64_t count = MAX(Array(renderer->get("surface_materials")).size(),
                      Array(renderer->get("material_parameter_names")).size());
  Array entries = renderer->get("mesh_entries");
  for (int64_t i = 0; i < entries.size(); ++i) {
    Ref<Resource> entry = entries[i];
    if (entry.is_null())
      continue;
    Ref<Mesh> mesh = entry->get("mesh");
    if (mesh.is_valid())
      count = MAX(count, mesh->get_surface_count());
  }
  return count;
}
TypedArray<Resource>
NBNativeEditorActions::user_entries(const Ref<Resource> &effect) {
  TypedArray<Resource> entries;
  if (effect.is_null())
    return entries;
  entries = Array(effect->get("user_parameters")).duplicate();
  Array names, settings = effect->get("settings");
  for (int64_t i = 0; i < entries.size(); ++i) {
    Ref<Resource> entry = entries[i];
    names.append(entry->get("input_name"));
  }
  for (int64_t i = 0; i < settings.size(); ++i) {
    Ref<Resource> entry = settings[i];
    String name = entry->get("input_name");
    if (name.begins_with("User.") && !names.has(name))
      entries.append(entry);
  }
  return entries;
}
Variant NBNativeEditorActions::edited_value(const Variant &original,
                                            const Array &path,
                                            const Variant &value) {
  Variant copy = original;
  if (copy.get_type() == Variant::DICTIONARY)
    copy = Dictionary(copy).duplicate(true);
  else if (copy.get_type() == Variant::ARRAY)
    copy = Array(copy).duplicate(true);
  return replace_path(copy, path, 0, value);
}
Dictionary NBNativeEditorActions::differences(const Ref<Resource> &target,
                                              const Dictionary &changes) {
  Dictionary result;
  Array properties = changes.keys();
  for (int64_t i = 0; i < properties.size(); ++i) {
    String property = properties[i];
    Variant value = changes[property], old = target->get(property);
    if (old.get_type() != value.get_type() || old != value)
      result[property] = value;
  }
  return result;
}
bool NBNativeEditorActions::edit_properties(Object *history,
                                            const Ref<Resource> &target,
                                            const Dictionary &changes,
                                            const String &label,
                                            const Callable &changed) {
  Dictionary values = differences(target, changes);
  if (values.is_empty())
    return false;
  history->call("create_action", label, 0);
  Array properties = values.keys();
  for (int64_t i = 0; i < properties.size(); ++i) {
    String property = properties[i];
    history->call("add_do_property", target, property, values[property]);
    history->call("add_undo_property", target, property, target->get(property));
  }
  history->call("add_do_method", changed);
  history->call("add_undo_method", changed);
  history->call("commit_action");
  return true;
}
Dictionary NBNativeEditorActions::move_module(
    Object *history, const Ref<Resource> &source, const Ref<Resource> &target,
    const Ref<Resource> &module, int64_t index, const Callable &changed) {
  Array source_ops = Array(source->get("operations")).duplicate(),
        target_ops = target->get("operations");
  int64_t old_index = source_ops.find(module);
  if (old_index < 0)
    return dict({{"changed", false}});
  index = CLAMP(index, int64_t(0), target_ops.size());
  if (source == target) {
    if (old_index < index)
      --index;
    if (old_index == index)
      return dict({{"changed", false}});
    source_ops.remove_at(old_index);
    source_ops.insert(index, module);
    edit_properties(history, source, dict({{"operations", source_ops}}),
                    "Move module", changed);
  } else {
    target_ops = target_ops.duplicate();
    source_ops.remove_at(old_index);
    target_ops.insert(index, module);
    history->call("create_action", String("Move module to ") +
                                       String(target->get("stage_name")));
    history->call("add_do_property", source, "operations", source_ops);
    history->call("add_do_property", target, "operations", target_ops);
    history->call("add_undo_property", source, "operations",
                  source->get("operations"));
    history->call("add_undo_property", target, "operations",
                  target->get("operations"));
    history->call("add_do_method", changed);
    history->call("add_undo_method", changed);
    history->call("commit_action");
  }
  return dict({{"changed", true}});
}
Dictionary NBNativeEditorActions::duplicate_emitters(
    const Ref<Resource> &effect, const TypedArray<Resource> &selected) {
  TypedArray<Resource> result, copies;
  if (effect.is_null())
    return dict({{"emitters", result}, {"copies", copies}});
  Array emitters = effect->get("emitters"), names;
  for (int64_t i = 0; i < emitters.size(); ++i) {
    Ref<Resource> emitter = emitters[i];
    names.append(emitter->get("display_name"));
  }
  for (int64_t i = 0; i < emitters.size(); ++i) {
    Ref<Resource> emitter = emitters[i];
    result.append(emitter);
    if (!selected.has(emitter))
      continue;
    Ref<Resource> copy =
        NBNativeDocumentCopy::copy(emitter, true, Dictionary());
    String base = String(emitter->get("display_name")) + String(" Copy"),
           name = base;
    int64_t number = 2;
    while (names.has(name))
      name = base + String(" ") + String::num_int64(number++);
    copy->set("display_name", name);
    names.append(name);
    result.append(copy);
    copies.append(copy);
  }
  return dict({{"emitters", result}, {"copies", copies}});
}
void NBNativeEditorActions::_bind_methods() {
  ClassDB::bind_static_method("NBNativeEditorActions",
                              D_METHOD("catalog", "entries", "stage"),
                              &NBNativeEditorActions::catalog);
  ClassDB::bind_static_method("NBNativeEditorActions",
                              D_METHOD("create_module", "entries", "name",
                                       "module_schema", "input_schema"),
                              &NBNativeEditorActions::create_module);
  ClassDB::bind_static_method("NBNativeEditorActions",
                              D_METHOD("validate_module_stage", "entries",
                                       "module", "stage", "original"),
                              &NBNativeEditorActions::validate_module_stage);
  ClassDB::bind_static_method(
      "NBNativeEditorActions",
      D_METHOD("capture_selection", "selected", "primary", "stage", "module"),
      &NBNativeEditorActions::capture_selection);
  ClassDB::bind_static_method(
      "NBNativeEditorActions",
      D_METHOD("resolve_selection", "effect", "selection"),
      &NBNativeEditorActions::resolve_selection);
  ClassDB::bind_static_method(
      "NBNativeEditorActions",
      D_METHOD("validate_selection", "effect", "selection"),
      &NBNativeEditorActions::validate_selection);
  ClassDB::bind_static_method("NBNativeEditorActions",
                              D_METHOD("owner_of_stage", "effect", "stage"),
                              &NBNativeEditorActions::owner_of_stage);
  ClassDB::bind_static_method(
      "NBNativeEditorActions",
      D_METHOD("selected_module_stage", "owner", "module"),
      &NBNativeEditorActions::selected_module_stage);
  ClassDB::bind_static_method(
      "NBNativeEditorActions",
      D_METHOD("contains_renderer", "effect", "renderer"),
      &NBNativeEditorActions::contains_renderer);
  ClassDB::bind_static_method("NBNativeEditorActions",
                              D_METHOD("material_slot_count", "renderer"),
                              &NBNativeEditorActions::material_slot_count);
  ClassDB::bind_static_method("NBNativeEditorActions",
                              D_METHOD("user_entries", "effect"),
                              &NBNativeEditorActions::user_entries);
  ClassDB::bind_static_method(
      "NBNativeEditorActions",
      D_METHOD("edited_value", "original", "path", "value"),
      &NBNativeEditorActions::edited_value);
  ClassDB::bind_static_method("NBNativeEditorActions",
                              D_METHOD("differences", "target", "changes"),
                              &NBNativeEditorActions::differences);
  ClassDB::bind_static_method("NBNativeEditorActions",
                              D_METHOD("edit_properties", "history", "target",
                                       "changes", "label", "changed"),
                              &NBNativeEditorActions::edit_properties);
  ClassDB::bind_static_method("NBNativeEditorActions",
                              D_METHOD("move_module", "history", "source",
                                       "target", "module", "index", "changed"),
                              &NBNativeEditorActions::move_module);
  ClassDB::bind_static_method(
      "NBNativeEditorActions",
      D_METHOD("duplicate_emitters", "effect", "selected"),
      &NBNativeEditorActions::duplicate_emitters);
}
} // namespace godot
