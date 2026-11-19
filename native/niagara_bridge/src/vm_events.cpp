#include "vm_events.h"
#include "vm_columns.h"
#include <cmath>
using namespace godot;
void NBNativeVMEvents::_bind_methods() {
    ClassDB::bind_static_method("NBNativeVMEvents", D_METHOD("graph", "plans"), &NBNativeVMEvents::graph);
    ClassDB::bind_method(D_METHOD("begin_frame"), &NBNativeVMEvents::begin_frame);
    ClassDB::bind_method(D_METHOD("read", "receiver", "settings"), &NBNativeVMEvents::read);
    ClassDB::bind_method(D_METHOD("publish", "emitter", "events"), &NBNativeVMEvents::publish);
    ClassDB::bind_method(D_METHOD("get_current"), &NBNativeVMEvents::get_current);
    ClassDB::bind_method(D_METHOD("get_previous"), &NBNativeVMEvents::get_previous);
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "current", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_current");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "previous", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_previous");
}
void NBNativeVMEvents::begin_frame() {
    previous = current;
    current = Dictionary();
}
Dictionary NBNativeVMEvents::read(const String &receiver, const Dictionary &settings) const {
    String source = settings.get("source_emitter", ""), event = settings.get("source_event", "");
    Dictionary empty_data;
    empty_data["float32"] = Array();
    empty_data["int32"] = Array();
    Dictionary empty;
    empty["data"] = empty_data;
    empty["count"] = 0;
    Dictionary buffers = receiver == source ? previous : current;
    Dictionary emitter = buffers.get(source, Dictionary());
    return emitter.get(event, empty);
}
TypedArray<String> NBNativeVMEvents::publish(const String &emitter, const Dictionary &events) {
    TypedArray<String> errors;
    Dictionary next = Dictionary(current.get(emitter, Dictionary())).duplicate();
    Array names = events.keys();
    for (int n = 0; n < names.size(); n++) {
        if (events[names[n]].get_type() != Variant::DICTIONARY) {
            errors.append("Invalid native event record.");
            return errors;
        }
        Dictionary incoming = events[names[n]];
        Variant count = incoming.get("count", Variant()), data = incoming.get("data", Variant());
        if ((count.get_type() != Variant::INT && count.get_type() != Variant::FLOAT) ||
            !std::isfinite(double(count)) || double(count) < 0 ||
            double(count) != std::floor(double(count)) || double(count) > 1000000 ||
            data.get_type() != Variant::DICTIONARY) {
            errors.append("Invalid native event count/data.");
            return errors;
        }
        Dictionary source_data = data;
        Dictionary target = Dictionary(next.get(names[n], incoming)).duplicate();
        Dictionary target_data = Dictionary(target.get("data", Dictionary())).duplicate();
        bool append = next.has(names[n]);
        for (String type : {String("float32"), String("int32")}) {
            if (source_data.get(type, Variant()).get_type() != Variant::ARRAY) {
                errors.append("Invalid native event columns.");
                return errors;
            }
            Array source_columns = source_data[type];
            Array target_columns = Array(target_data.get(type, Array())).duplicate();
            for (int c = 0; c < source_columns.size(); c++) {
                Variant column = source_columns[c];
                if (column.get_type() != Variant::ARRAY &&
                    column.get_type() !=
                        (type == "float32" ? Variant::PACKED_FLOAT32_ARRAY : Variant::PACKED_INT32_ARRAY)) {
                    errors.append("Invalid typed native event column.");
                    return errors;
                }
                nbvm::ColumnView view(column);
                if (view.size != 0 && view.size != int64_t(count)) {
                    errors.append("Short native event column.");
                    return errors;
                }
                while (target_columns.size() <= c)
                    target_columns.append(Array());
                if (!append) {
                    target_columns[c] =
                        column.get_type() == Variant::ARRAY ? Variant(Array(column).duplicate(true)) : column;
                    continue;
                }
                Variant old = target_columns[c];
                if (old.get_type() == Variant::PACKED_FLOAT32_ARRAY ||
                    column.get_type() == Variant::PACKED_FLOAT32_ARRAY) {
                    PackedFloat32Array values = old.get_type() == Variant::ARRAY
                                                    ? PackedFloat32Array(Array(old))
                                                    : PackedFloat32Array(old);
                    values.append_array(column.get_type() == Variant::ARRAY
                                            ? PackedFloat32Array(Array(column))
                                            : PackedFloat32Array(column));
                    target_columns[c] = values;
                } else if (old.get_type() == Variant::PACKED_INT32_ARRAY ||
                           column.get_type() == Variant::PACKED_INT32_ARRAY) {
                    PackedInt32Array values = old.get_type() == Variant::ARRAY ? PackedInt32Array(Array(old))
                                                                               : PackedInt32Array(old);
                    values.append_array(column.get_type() == Variant::ARRAY ? PackedInt32Array(Array(column))
                                                                            : PackedInt32Array(column));
                    target_columns[c] = values;
                } else {
                    Array values = Array(old).duplicate();
                    values.append_array(column);
                    target_columns[c] = values;
                }
            }
            target_data[type] = target_columns;
        }
        target["data"] = target_data;
        target["count"] = int64_t(count) + (append ? int64_t(target.get("count", 0)) : 0);
        next[names[n]] = target;
    }
    current[emitter] = next;
    return errors;
}
