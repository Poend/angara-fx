#include "text_saver.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
namespace godot {
void NBNativeTextSaver::_bind_methods() {
    ClassDB::bind_static_method("NBNativeTextSaver", D_METHOD("make_byte_arrays_readable", "path"),
                                &NBNativeTextSaver::make_byte_arrays_readable);
    ClassDB::bind_static_method("NBNativeTextSaver", D_METHOD("save", "resource", "path"),
                                &NBNativeTextSaver::save);
}
int64_t NBNativeTextSaver::make_byte_arrays_readable(const String &path) {
    if (!path.ends_with(".tres") && !path.ends_with(".tscn"))
        return ERR_INVALID_PARAMETER;
    Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
    if (file.is_null())
        return FileAccess::get_open_error();
    CharString utf8 = file->get_as_text().utf8();
    file.unref();
    std::string_view input(utf8.get_data(), utf8.length());
    constexpr std::string_view marker = "PackedByteArray(\"";
    std::string output;
    size_t search = 0, copied = 0;
    bool changed = false;
    static const std::array<std::string, 256> decimal = []() {
        std::array<std::string, 256> result;
        for (size_t n = 0; n < result.size(); ++n)
            result[n] = std::to_string(n);
        return result;
    }();
    auto base64 = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
               c == '/';
    };
    while ((search = input.find(marker, search)) != std::string_view::npos) {
        size_t start = search + marker.size(), end = start;
        while (end < input.size() && base64(input[end]))
            ++end;
        for (int n = 0; n < 2 && end < input.size() && input[end] == '='; ++n)
            ++end;
        if (end + 1 >= input.size() || input[end] != '"' || input[end + 1] != ')') {
            search = start;
            continue;
        }
        if (!changed)
            output.reserve(input.size());
        changed = true;
        output.append(input.data() + copied, search - copied);
        output.append("PackedByteArray(");
        PackedByteArray bytes =
            Marshalls::get_singleton()->base64_to_raw(String::utf8(input.data() + start, end - start));
        const uint8_t *data = bytes.ptr();
        for (int64_t n = 0; n < bytes.size(); ++n) {
            if (n)
                output.append(", ");
            output.append(decimal[data[n]]);
        }
        output.push_back(')');
        copied = end + 2;
        search = copied;
    }
    if (!changed)
        return OK;
    output.append(input.data() + copied, input.size() - copied);
    file = FileAccess::open(path, FileAccess::WRITE);
    if (file.is_null())
        return FileAccess::get_open_error();
    // The standard text loader remains the only reader; no custom binary blob.
    PackedByteArray bytes;
    bytes.resize(output.size());
    if (!output.empty())
        std::memcpy(bytes.ptrw(), output.data(), output.size());
    file->store_buffer(bytes);
    file->flush();
    return file->get_error();
}
int64_t NBNativeTextSaver::save(const Ref<Resource> &resource, const String &path) {
    if (!path.ends_with(".tres") && !path.ends_with(".tscn"))
        return ERR_INVALID_PARAMETER;
    Error error = ResourceSaver::get_singleton()->save(resource, path);
    return error == OK ? make_byte_arrays_readable(path) : int64_t(error);
}
} // namespace godot
