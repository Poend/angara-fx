#pragma once
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/script.hpp>
#include <optional>
#include <unordered_map>
#include <vector>

namespace godot {
struct NBShaderUniforms {
    String code;
    std::vector<Variant> names;
};

// Per-instance caches expire together when a saved effect is installed.
class NBNativeEffectRevision : public RefCounted {
    GDCLASS(NBNativeEffectRevision, RefCounted)
    std::optional<Dictionary> parameter_defaults;
    Dictionary parameter_types;
  protected:
    static void _bind_methods() {}
  public:
    static Dictionary parameters(Node *player, const Ref<Script> &compiler);
    // Owned by one player for the lifetime of one compiled, saved document.
    std::unordered_map<uint64_t,std::vector<uint32_t>> saved_graph_fields;
    std::unordered_map<uint64_t,int64_t> saved_validation_stamps;
    std::unordered_map<uint64_t,NBShaderUniforms> saved_uniform_names;
    std::unordered_map<uint64_t,String> saved_camera_offsets;
    std::optional<uint32_t> saved_order_hash;
    inline static thread_local NBNativeEffectRevision *current_revision = nullptr;
    class RevisionScope {
        NBNativeEffectRevision *previous;
      public:
        explicit RevisionScope(NBNativeEffectRevision *revision) : previous(current_revision) {
            current_revision = revision;
        }
        ~RevisionScope() { current_revision = previous; }
    };
};
} // namespace godot
