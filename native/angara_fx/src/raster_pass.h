#pragma once
#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/script.hpp>
#include <mutex>
#include <unordered_map>
namespace godot {
// Authored resources stay in Godot text files; all device state is transient.
class NBNativeRasterPass : public CompositorEffect {
    GDCLASS(NBNativeRasterPass, CompositorEffect)
    mutable std::mutex lock;
    mutable std::mutex gpu_lock;
    bool surface_mode = false, disposed = false;
    Dictionary options, dependencies, players, submissions, light_submissions, programs, gpu_programs;
    Dictionary report, view, height_fog, sort_override, lighting_config, lighting_gpu;
    Array retired;
    RID distortion_offsets, distortion_color, distortion_framebuffer;
    Vector2i distortion_size;
    bool distortion_precision = false;
    struct GeometryBuffer {
        PackedByteArray data;
        RID buffer;
        std::unordered_map<uint64_t, RID> sets;
        uint64_t used = 0;
    };
    struct IndexBuffer {
        PackedInt32Array data;
        RID buffer, array;
        uint64_t used = 0;
    };
    uint64_t draw_generation = 0;
    std::unordered_map<const uint8_t *, GeometryBuffer> geometries;
    std::unordered_map<const int32_t *, IndexBuffer> index_buffers;
    Dictionary draw_samplers;
    int64_t sampler_level = -1;
    void retire_geometry(Array &resources, bool all);
    RID geometry_set(RenderingDevice *rd, const PackedByteArray &bytes, const RID &shader);
    RID index_array(RenderingDevice *rd, const PackedInt32Array &indices);
    RID draw_sampler(RenderingDevice *rd, const String &hints);

    RID fallback, apply_shader, apply_pipeline, copy_shader, copy_pipeline;
    String copy_source, apply_source, lighting_key;
    void fail(const String &message);
    Array collect_lights(); // lock held
    void render_frame(RenderData *data);
    Dictionary gpu_program(RenderingDevice *rd, const Dictionary &draw, const RID &framebuffer,
                           bool precision);
    Array prepare_draws(RenderingDevice *rd, const Array &draws, const RID &framebuffer, const RID &depth,
                        const Vector2i &size, const Projection &projection, const Transform3D &camera,
                        const Dictionary &frame_values, const Dictionary &lighting_textures, Array resources,
                        bool precision);
    bool compute_shader(RenderingDevice *rd, const String &source, RID &shader, RID &pipeline);
    void release_gpu(RenderingDevice *rd);

  protected:
    static void _bind_methods();

  public:
    NBNativeRasterPass();
    ~NBNativeRasterPass();
    void initialize_pass(bool surface, const Dictionary &types, const String &copy = String(),
                         const String &apply = String());
    void set_option(const String &key, const Variant &value);
    Variant get_option(const String &key) const;
    Dictionary statistics();
    Dictionary get_players() const;
    Array get_retired() const;
    String get_lighting_key() const;
    Dictionary get_height_fog() const;
    Dictionary get_sort_override() const;
    void dispose();
    void dispose_gpu();
    void _render_callback(int32_t callback_type, RenderData *data) override;
    String set_view_parameters(double exposure, double limit, const Vector4 &fog);
    TypedArray<String> set_height_fog(const Ref<Resource> &settings);
    TypedArray<String> prepare_player_view(Camera3D *camera);
    TypedArray<String> submit_surface(Node *owner, MeshInstance3D *instance,
                                      const Dictionary &sort_context = Dictionary());
    void remove_surface(Node *owner);
    TypedArray<String> submit_particle_lights(Node *owner, const Array &records, double exposure);
    TypedArray<String> submit_player_particle_lights(Node *owner, const Array &records, Camera3D *camera);
    void remove_particle_lights(Node *owner);
    double sort_distance(const Dictionary &group, const Transform3D &camera);
    Array ordered_groups(Array groups, const Transform3D &camera);
    void submit_player(Node *owner, const Array &instances);
    void remove_player(Node *owner);
    static Ref<RDUniform> uniform(int64_t type, int64_t binding, const Array &ids);
    static RID texture(RenderingDevice *rd, const Vector2i &size, int64_t format, int64_t usage,
                       const TypedArray<PackedByteArray> &data = TypedArray<PackedByteArray>());
    static RID sampler(RenderingDevice *rd, const String &hints);
    static void release(RenderingDevice *rd, const Array &resources);
    static void release_all(RenderingDevice *rd, Array retired, Dictionary programs, const Array &persistent);
};
} // namespace godot
