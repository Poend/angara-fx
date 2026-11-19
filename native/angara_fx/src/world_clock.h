#pragma once
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
namespace godot {
// One clock per SceneTree. Pausing freezes game time; real time keeps advancing.
class NBNativeWorldClock : public Node {
    GDCLASS(NBNativeWorldClock, Node)
    double game_time = 0;
    uint64_t real_start = 0;

  protected:
    static void _bind_methods();

  public:
    NBNativeWorldClock();
    void _process(double delta) override;
    Dictionary snapshot() const;
    static Dictionary values(Node *host);
};
} // namespace godot
