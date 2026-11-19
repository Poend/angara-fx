#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/script.hpp>
#include <initializer_list>
#include <utility>
namespace godot::nbdocument {
Dictionary dict(std::initializer_list<std::pair<String, Variant>> values);
bool number(const Variant &v);
bool equal(const Variant &a, const Variant &b);
void append(const Ref<Resource> &r, const String &property, const Variant &v);
}
