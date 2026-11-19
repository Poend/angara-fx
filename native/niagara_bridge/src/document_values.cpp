#include "document_values.h"

namespace godot::nbdocument {
Dictionary dict(std::initializer_list<std::pair<String, Variant>> values) {
    Dictionary result;
    for (const auto &v : values)
        result[v.first] = v.second;
    return result;
}
bool number(const Variant &v) {
    return v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT;
}
bool equal(const Variant &a, const Variant &b) {
    return a.get_type() == b.get_type() ? a == b : (number(a) && number(b) && double(a) == double(b));
}
void append(const Ref<Resource> &r, const String &property, const Variant &v) {
    Array a = r->get(property);
    a.append(v);
}
}
