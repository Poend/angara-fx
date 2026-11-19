#include "particle_buffer.h"
#include "vm_datasets.h"
#include "vm_columns.h"
#include <godot_cpp/core/class_db.hpp>
#include <cstring>

namespace godot {
namespace {
// The two programs may store the same attribute at different offsets.
struct ColumnLayout {
    struct Field {
        Variant name, type;
        int64_t start, count;
    };
    std::vector<Field> fields;
    Dictionary by_name;
    ColumnLayout(const Array &layout, const String &type) {
        const Variant start_key = type + String("_start"), count_key = type + String("_components");
        const Variant name_key = String("name"), type_key = String("type");
        fields.reserve(layout.size());
        for (int64_t i = 0; i < layout.size(); ++i) {
            Dictionary row = layout[i];
            Field field{row.get(name_key, ""), row.get(type_key, ""), row.get(start_key, 0), row.get(count_key, 0)};
            if (!by_name.has(field.name)) by_name[field.name] = i;
            fields.push_back(std::move(field));
        }
    }
    const Field *at(int index) const {
        for (const auto &field : fields)
            if (index >= field.start && index - field.start < field.count) return &field;
        return nullptr;
    }
    Variant column(const Array &columns, const Field &target, int component) const {
        const int64_t slot = by_name.get(target.name, -1);
        if (slot < 0) return Variant();
        const auto &source = fields[size_t(slot)];
        if (source.type != target.type || component >= source.count) return Variant();
        const int64_t index = source.start + component;
        return index >= 0 && index < columns.size() ? columns[index] : Variant();
    }
};
Array metadata_only(const Array &particles, const Array &variables) {
    Array result;
    std::vector<String> attribute_names;
    for (int v = 0; v < variables.size(); ++v) {
        Dictionary row = variables[v];
        attribute_names.push_back(String("Particles.") + String(row["name"]));
    }
    for (int n = 0; n < particles.size(); ++n) {
        Dictionary particle = Dictionary(particles[n]).duplicate();
        particle.erase("_vm_fields");
        Dictionary attributes = Dictionary(particle.get("attributes", Dictionary())).duplicate();
        for (const String &name : attribute_names)
            attributes.erase(name);
        particle["attributes"] = attributes;
        result.append(particle.duplicate(true));
    }
    return result;
}
} // namespace
void NBNativeParticleBuffer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("snapshot", "include_raw_fields"), &NBNativeParticleBuffer::snapshot,
                         DEFVAL(true));
    ClassDB::bind_method(D_METHOD("metadata_rows"),&NBNativeParticleBuffer::metadata_rows);
    ClassDB::bind_method(D_METHOD("append", "other"), &NBNativeParticleBuffer::append);
    ClassDB::bind_method(D_METHOD("get_view"), &NBNativeParticleBuffer::get_view);
    ClassDB::bind_method(D_METHOD("duplicate_buffer"), &NBNativeParticleBuffer::duplicate_buffer);
    ClassDB::bind_method(D_METHOD("get_count"), &NBNativeParticleBuffer::get_count);
    ClassDB::bind_method(D_METHOD("get_statistics"), &NBNativeParticleBuffer::get_statistics);
    ClassDB::bind_method(D_METHOD("prepare_tick_metadata"), &NBNativeParticleBuffer::prepare_tick_metadata);
}
void NBNativeParticleBuffer::initialize(const Ref<NBNativeVMParticles> &adapter, const Dictionary &data,
                                        const Array &metadata, bool import_metadata) {
    cached_view.unref();
    projector = adapter;
    variables = adapter->source_layout();
    layout_revision = adapter->get_layout_revision();
    dataset = data;
    templates = import_metadata ? metadata_only(metadata, variables) : metadata;
    update_indices.clear();
    update_counts.clear();
}
Dictionary NBNativeParticleBuffer::input_for(const Ref<NBNativeVMParticles> &adapter,
                                             const Array &bindings) const {
    Dictionary result, mapped;
    TypedArray<String> errors;
    result["errors"] = errors;
    if (projector == adapter && layout_revision == adapter->get_layout_revision()) {
        // A normal update consumes the same persisted schema it just wrote.
        // Retain columns directly; the executor still checks types and lengths.
        result["dataset"] = dataset;
        return result;
    }
    const Array &layout = adapter->source_layout();
    const ColumnLayout target_floats(layout, "float32"), target_ints(layout, "int32");
    const ColumnLayout source_floats(variables, "float32"), source_ints(variables, "int32");
    const Array float_columns = dataset.get("float32", Array()), int_columns = dataset.get("int32", Array());
    mapped["float32"] = Array();
    mapped["int32"] = Array();
    result["errors"] = errors;
    result["dataset"] = mapped;
    for (int n = 0; n < bindings.size(); ++n) {
        Dictionary binding = bindings[n];
        if (int64_t(binding.get("dataset", 0)) != 0)
            continue;
        String type = binding.get("type", "");
        int index = binding.get("component", -1);
        const bool integer = type == "int32";
        const ColumnLayout &target = integer ? target_ints : target_floats;
        const ColumnLayout &source = integer ? source_ints : source_floats;
        const auto *field = target.at(index);
        Variant column;
        if (field && (integer || type == "float32"))
            column = source.column(integer ? int_columns : float_columns, *field, index - int(field->start));
        nbvm::ColumnView view(column);
        if (!field || (!integer && type != "float32") || view.size != templates.size()) {
            errors.append("Resident particle buffer lacks compatible input: " +
                          (field ? String(field->name) : String("?")) + "/" + type);
            return result;
        }
        Array output = mapped[type];
        while (output.size() <= index)
            output.append(Array());
        output[index] = column;
    }
    return result;
}
Dictionary NBNativeParticleBuffer::snapshot(bool include_raw_fields) const {
    if (projector.is_null()) {
        Dictionary result;
        TypedArray<String> errors;
        errors.append("Uninitialized resident particle buffer.");
        result["errors"] = errors;
        result["particles"] = Array();
        return result;
    }
    Array kept;
    kept.resize(templates.size());
    for (int i = 0; i < templates.size(); ++i)
        kept[i] = i;
    return NBNativeVMParticles::project_dataset(dataset, templates, kept, variables, include_raw_fields, true,
                                                update_indices, update_counts);
}
Ref<NBNativeParticleView> NBNativeParticleBuffer::get_view() const {
    if (cached_view.is_valid()) return cached_view;
    cached_view = NBNativeVMParticles::create_view(dataset, templates, variables, update_indices, update_counts);
    if (projector.is_null()) cached_view->add_error("Uninitialized resident particle buffer.");
    return cached_view;
}
Array NBNativeParticleBuffer::metadata_rows() const {
    Array result;result.resize(templates.size());
    auto view=get_view();int64_t id=view->field_handle("id");
    const Variant id_key=String("id");
    for(int64_t i=0;i<templates.size();++i) {
        Dictionary row;row[id_key]=view->read(id,i);result[i]=row;
    }
    return result;
}
void NBNativeParticleBuffer::prepare_tick_metadata() {
    cached_view.unref();
    int64_t count = templates.size();
    update_indices.resize(count);
    update_counts.resize(count);
    int32_t *indices = update_indices.ptrw(), *counts = update_counts.ptrw();
    for (int64_t i = 0; i < count; ++i) {
        indices[i] = int32_t(i);
        counts[i] = int32_t(count);
    }
}
void NBNativeParticleBuffer::inherit_tick_metadata(const Ref<NBNativeParticleBuffer> &source,
                                                   const Array &kept) {
    cached_view.unref();
    if (source->update_indices.is_empty())
        return;
    PackedInt32Array source_indices = source->update_indices, source_counts = source->update_counts;
    update_indices.resize(kept.size());
    update_counts.resize(kept.size());
    int32_t *indices = update_indices.ptrw(), *counts = update_counts.ptrw();
    for (int64_t i = 0; i < kept.size(); ++i) {
        int64_t original = kept[i];
        indices[i] = source_indices[original];
        counts[i] = source_counts[original];
    }
}
Ref<NBNativeParticleBuffer> NBNativeParticleBuffer::duplicate_buffer() const {
    Ref<NBNativeParticleBuffer> result;
    result.instantiate();
    result->projector = projector;
    result->layout_revision = layout_revision;
    result->variables = variables;
    result->dataset = dataset.duplicate();
    result->templates = templates.duplicate();
    result->update_indices = update_indices;
    result->update_counts = update_counts;
    return result;
}
TypedArray<String> NBNativeParticleBuffer::append(const Ref<NBNativeParticleBuffer> &other) {
    TypedArray<String> errors;
    if (other.is_null() || other->projector.is_null()) {
        errors.append("Invalid resident particle buffer append.");
        return errors;
    }
    if (projector.is_null()) {
        cached_view.unref();
        projector = other->projector;
        layout_revision = other->layout_revision;
        variables = other->variables;
        dataset = other->dataset.duplicate();
        templates = other->templates.duplicate();
        update_indices = other->update_indices;
        update_counts = other->update_counts;
        return errors;
    }
    if (variables.size() != other->variables.size()) {
        errors.append("Resident particle append layout differs.");
        return errors;
    }
    Dictionary joined;
    for (String type : {String("float32"), String("int32")}) {
        Array source = dataset.get(type, Array()), output;
        output.resize(source.size());
        const ColumnLayout left_layout(variables, type), right_layout(other->variables, type);
        const Array right_columns = other->dataset.get(type, Array());
        for (int index = 0; index < source.size(); ++index) {
            const auto *field = left_layout.at(index);
            if (!field) {
                output[index] = source[index];
                continue;
            }
            nbvm::ColumnView left(source[index]),
                right(right_layout.column(right_columns, *field, index - int(field->start)));
            if (left.size != templates.size() || right.size != other->templates.size()) {
                errors.append("Resident particle append lacks compatible attribute: " +
                              String(field->name));
                return errors;
            }
            if (type == "float32") {
                PackedFloat32Array values;
                values.resize(left.size + right.size);
                float *out = values.ptrw();
                if (left.float_data)
                    std::memcpy(out, left.float_data, left.size * sizeof(float));
                else
                    for (int i = 0; i < left.size; ++i)
                        out[i] = float(double(left.at(i)));
                if (right.float_data)
                    std::memcpy(out + left.size, right.float_data, right.size * sizeof(float));
                else
                    for (int i = 0; i < right.size; ++i)
                        out[left.size + i] = float(double(right.at(i)));
                output[index] = values;
            } else {
                PackedInt32Array values;
                values.resize(left.size + right.size);
                int32_t *out = values.ptrw();
                if (left.int_data)
                    std::memcpy(out, left.int_data, left.size * sizeof(int32_t));
                else
                    for (int i = 0; i < left.size; ++i)
                        out[i] = int32_t(int64_t(left.at(i)));
                if (right.int_data)
                    std::memcpy(out + left.size, right.int_data, right.size * sizeof(int32_t));
                else
                    for (int i = 0; i < right.size; ++i)
                        out[left.size + i] = int32_t(int64_t(right.at(i)));
                output[index] = values;
            }
        }
        joined[type] = output;
    }
    PackedInt32Array joined_indices, joined_counts;
    if (!update_indices.is_empty() || !other->update_indices.is_empty()) {
        int64_t left_count = templates.size(), right_count = other->templates.size();
        joined_indices.resize(left_count + right_count);
        joined_counts.resize(left_count + right_count);
        joined_indices.fill(-1);
        joined_counts.fill(-1);
        int32_t *indices = joined_indices.ptrw(), *counts = joined_counts.ptrw();
        if (!update_indices.is_empty()) {
            std::memcpy(indices, update_indices.ptr(), left_count * sizeof(int32_t));
            std::memcpy(counts, update_counts.ptr(), left_count * sizeof(int32_t));
        }
        if (!other->update_indices.is_empty()) {
            std::memcpy(indices + left_count, other->update_indices.ptr(), right_count * sizeof(int32_t));
            std::memcpy(counts + left_count, other->update_counts.ptr(), right_count * sizeof(int32_t));
        }
    }
    // Only publish after all columns validate. Packed arrays detach on write.
    cached_view.unref();
    dataset = joined;
    update_indices = joined_indices;
    update_counts = joined_counts;
    templates = templates.duplicate();
    templates.append_array(other->templates);
    return errors;
}
Dictionary NBNativeParticleBuffer::get_statistics() const {
    Dictionary result;
    result["native_resident"] = true;
    result["particles"] = templates.size();
    result["attributes"] = variables.size();
    int64_t bytes = 0;
    for (String type : {String("float32"), String("int32")}) {
        Array columns = dataset.get(type, Array());
        for (int n = 0; n < columns.size(); ++n)
            bytes += nbvm::ColumnView(columns[n]).size * 4;
    }
    result["column_bytes"] = bytes;
    result["tick_metadata_bytes"] = update_indices.size() * 8;
    return result;
}
} // namespace godot
