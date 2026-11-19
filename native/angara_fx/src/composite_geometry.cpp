#include "renderer_geometry.h"
#include "ribbon_geometry.h"
#include "value_evaluator.h"
#include "attribute_values.h"
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <chrono>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <algorithm>
namespace godot {
namespace {
Array ribbon_points(const NBRendererRows &rows) {
    // Only these bound attributes enter ribbon topology, UVs and vertex interpolation.
    constexpr const char *fields[] = {"id", "position", "normalized_age", "ribbon_width", "ribbon_facing",
                                      "color", "dynamic", "dynamic1", "dynamic2", "dynamic3"};
    struct Column { String key; int64_t handle; };
    std::vector<Column> columns;
    for (const char *field : fields) {
        int64_t handle = rows.source->field_handle(field);
        if (handle >= 0)
            columns.push_back({field, handle});
    }
    Array points;
    points.resize(rows.rows.size());
    for (size_t i = 0; i < rows.rows.size(); ++i) {
        Dictionary point;
        for (const auto &column : columns) {
            Variant value = rows.field(rows.rows[i], column.handle, Variant());
            if (value.get_type() != Variant::NIL)
                point[column.key] = value;
        }
        points[i] = point;
    }
    return points;
}
} // namespace
NBGeometryPacket NBNativeRendererGeometry::composite_packet(const Array &states, Camera3D *camera,
                                                       const Transform3D &owner, const Dictionary &frame,
                                                       const Dictionary &builtins, int64_t tick) {
    NBGeometryPacket result;
    Node3D *profile_host=nullptr;
    if(!states.is_empty()) {
        Dictionary first=states[0];Object *raw=first.get("host",Variant());
        auto *host=Object::cast_to<Node3D>(raw);
        if(host && bool(host->get_meta("angara_fx_profile_renderer",false))) profile_host=host;
    }
    Dictionary phases;
    auto phase_start=profile_host ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    auto mark=[&](const char *name) {
        if(!profile_host) return;
        auto now=std::chrono::steady_clock::now();
        phases[name]=double(phases.get(name,0.))+std::chrono::duration<double,std::micro>(now-phase_start).count();phase_start=now;
    };
    auto &records = result.records;
    auto &diagnostics = result.diagnostics;
    std::optional<AABB> bounds;
    auto merge = [&](const AABB &current) {
        bounds = bounds ? bounds->merge(current) : current;
    };
    for (int n = 0; n < states.size(); ++n) {
        Dictionary packet = states[n], state = packet["state"], emitter = state["emitter"],
                   renderer = state["renderer"], plan = emitter["plan"];
        Object *object = renderer.get("instance", Variant());
        auto *instance = Object::cast_to<Node3D>(object);
        if (bool(renderer.get("scoped_hidden", false)) && instance)
            instance->set_visible(false);
        if (bool(emitter.get("curve_parameters_invalid", false)))
            continue;
        Ref<Resource> source = renderer["source"];
        bool visible = packet["visible"], local = plan["local_space"];
        Transform3D space = local ? owner : Transform3D();
        Array filtered = packet["particles"], particles;
        int64_t kind = state["kind"];
        if (kind!=2 && bool(packet.get("resident_rows",false))) {
            mark("packet_setup");
            NBRendererRows rows=NBNativeRendererViews::resident_rows(emitter,source,packet["parameters"],
                                                                     owner.origin,frame,builtins);
            mark("resident_bindings");
            if (rows.source.is_valid()) {
                Object *raw=packet.get("host",Variant());
                if (auto *host=Object::cast_to<Node3D>(raw)) renderer_view_errors(host,emitter,source,rows.errors);
                if (!rows.errors.is_empty()) continue;
                rows.sort(source,camera,space);
                mark("resident_sort");
                NBSpriteFrame sprite_frame;NBSpriteColumns columns;
                if(kind!=3) {sprite_frame=prepare_sprite_frame(renderer,camera,space);columns=sprite_columns(sprite_frame,rows.source);}
                Dictionary binding=renderer.get("mesh_binding",Dictionary());Ref<Resource> entry=binding.get("entry",Variant());
                if(kind==3 && entry.is_null()) {
                    Dictionary diagnostic;diagnostic["severity"]="error";Ref<Resource> emitter_source=plan["source"];
                    diagnostic["emitter"]=emitter_source->get("display_name");diagnostic["message"]="Mesh renderer has no valid compiled entry.";
                    diagnostics.append(diagnostic);continue;
                }
                int64_t mesh_index=rows.source->field_handle("mesh_index"),position=rows.source->field_handle("position"),
                        velocity=rows.source->field_handle("velocity"),scale=rows.source->field_handle("mesh_scale"),orientation=rows.source->field_handle("mesh_orientation");
                int64_t color=rows.source->field_handle("color"),subimage=rows.source->field_handle("subimage_index"),
                        random=rows.source->field_handle("material_random"),age=rows.source->field_handle("normalized_age");
                int64_t dynamic[4];for(int i=0;i<4;++i) dynamic[i]=rows.source->field_handle(i==0 ? String("dynamic") : String("dynamic")+String::num_int64(i));
                String offset_name=packet["offset_name"];
                bool fixed=!offset_name.begins_with("Particles.");
                double shared=fixed ? camera_offset(offset_name,Dictionary(),emitter,frame,builtins) : 0.;
                int64_t offset_handle=-1;
                if(!fixed) {
                    if(offset_name=="Particles.SpriteRotation") offset_handle=rows.source->binding_handle(offset_name,builtins);
                    else if(builtins.has(offset_name)) offset_handle=rows.source->field_handle(Array(builtins[offset_name])[1]);
                    else offset_handle=rows.source->attribute_handle(offset_name);
                }
                const auto coverage_frame=prepare_coverage(renderer.get("pixel_coverage",Dictionary()),camera);
                AABB mesh_bounds=renderer["mesh_aabb"];
                const int64_t context_slot = state["context_slot"];
                if (visible) records.reserve(records.size() + rows.rows.size());
                mark("geometry_setup");
                const size_t first_record = records.size();
                const size_t chunk_size = std::clamp<size_t>((rows.rows.size() + 3) / 4, 32, 256);
                // Mesh facing can consult scene resources. Sprite inputs are already
                // retained, and the camera, mappings and sort order are finalized.
                auto *pool = kind != 3 && rows.rows.size() >= 96 ? WorkerThreadPool::get_singleton() : nullptr;
                const bool parallel = pool && pool->get_caller_task_id() < 0 && pool->get_caller_group_id() < 0;
                std::vector<AABB> row_bounds;
                if (parallel) {
                    row_bounds.resize(rows.rows.size());
                    if (visible) records.resize(first_record + rows.rows.size());
                }
                const bool detail = profile_host && !parallel;
                auto emit_row = [&](size_t index) {
                    const auto &row = rows.rows[index];
                    if(kind==3 && int64_t(rows.field(row,mesh_index))!=int64_t(binding.get("source_index",0))) return;
                    Variant offset_value=fixed ? Variant(shared) : rows.source->read(offset_handle,row.lane);
                    auto row_start=detail ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
                    Transform3D transform;Color color_value=rows.field_native<Color>(row,color),attributes_value;
                    if(kind==3) {
                        NBMeshRow particle;particle.position=rows.field_native<Vector3>(row,position);particle.velocity=rows.field_native<Vector3>(row,velocity);
                        particle.scale=rows.field_native<Vector3>(row,scale);particle.orientation=rows.field_native<Quaternion>(row,orientation);
                        transform=mesh_transform_row(entry,particle,local,owner,camera,double(offset_value),binding.get("facing",Dictionary()));
                        attributes_value=Color(rows.field_native<double>(row,subimage),rows.field_native<double>(row,random),1,1);
                    } else {
                        const auto sprite=sprite_renderer_row(sprite_frame,columns,rows,row);
                        if(detail) {auto now=std::chrono::steady_clock::now();phases["detail_sprite_reads"]=double(phases.get("detail_sprite_reads",0.))+std::chrono::duration<double,std::micro>(now-row_start).count();row_start=now;}
                        const auto footprint=coverage_prepared(coverage_frame,sprite.size,space.xform(NBNativeValues::unreal_to_godot(sprite.position)));
                        if(detail) {auto now=std::chrono::steady_clock::now();phases["detail_coverage"]=double(phases.get("detail_coverage",0.))+std::chrono::duration<double,std::micro>(now-row_start).count();row_start=now;}
                        transform=sprite_transform_row(sprite_frame,sprite,double(offset_value),footprint.size_ratio);color_value*=footprint.color_scale;
                        attributes_value=Color(rows.field_native<double>(row,subimage),rows.field_native<double>(row,random),sprite.uv_x*(sprite.size.x<0 ? -1. : 1.),sprite.uv_y*(sprite.size.y<0 ? -1. : 1.));
                    }
                    if(detail) {auto now=std::chrono::steady_clock::now();phases["detail_transform"]=double(phases.get("detail_transform",0.))+std::chrono::duration<double,std::micro>(now-row_start).count();row_start=now;}
                    AABB box = transform.xform(mesh_bounds);
                    if (parallel) row_bounds[index] = box;
                    else merge(box);
                    if (!visible) return;
                    NBGeometryRecord &record = parallel ? records[first_record + index] : records.emplace_back();
                    record.transform=transform;
                    record.color=color_value;
                    record.dynamic=rows.field_native<Color>(row,dynamic[0]);record.dynamic1=rows.field_native<Color>(row,dynamic[1]);
                    record.dynamic2=rows.field_native<Color>(row,dynamic[2]);record.dynamic3=rows.field_native<Color>(row,dynamic[3]);
                    record.attributes=attributes_value;
                    if(kind==3) {record.arrays=Array(state["arrays"]);record.geometry_colors=true;}
                    record.context_slot=context_slot;record.relative_time=rows.field_native<double>(row,age);
                    if(detail) {auto now=std::chrono::steady_clock::now();phases["detail_record"]=double(phases.get("detail_record",0.))+std::chrono::duration<double,std::micro>(now-row_start).count();}
                };
                if (parallel) {
                    const uint32_t chunks = uint32_t((rows.rows.size() + chunk_size - 1) / chunk_size);
                    auto emit_chunk = [&](uint32_t chunk) {
                        size_t end = std::min(rows.rows.size(), (size_t(chunk) + 1) * chunk_size);
                        for (size_t i = size_t(chunk) * chunk_size; i < end; ++i) emit_row(i);
                    };
                    const auto task = pool->add_native_group_task([](void *data, uint32_t i) {
                        (*static_cast<decltype(emit_chunk) *>(data))(i + 1);
                    }, &emit_chunk, chunks - 1, std::min(3u, chunks - 1), true);
                    emit_chunk(0);
                    pool->wait_for_group_task_completion(task);
                    for (const AABB &box : row_bounds) merge(box);
                } else {
                    for (size_t i = 0; i < rows.rows.size(); ++i) emit_row(i);
                }
                mark("resident_geometry");
                continue;
            }
        }
        if (kind == 2) {
            if (bool(packet.get("resident_rows", false))) {
                NBRendererRows rows = NBNativeRendererViews::resident_rows(emitter, source, packet["parameters"],
                                                                          owner.origin, frame, builtins);
                if (auto *host = Object::cast_to<Node3D>(packet["host"].get_validated_object()))
                    renderer_view_errors(host, emitter, source, rows.errors);
                if (rows.source.is_null() || !rows.errors.is_empty())
                    continue;
                filtered = ribbon_points(rows);
            }
            Dictionary geometry = NBNativeRibbonGeometry::build(filtered, source, space, camera,
                                                                renderer["history"], false),
                       statistics;
            renderer["last_tick"] = tick;
            for (const char *key : {"points", "segments", "subdivisions"})
                statistics[key] = geometry[key];
            statistics["float_vertex_colors"] = true;
            renderer["geometry_statistics"] = statistics;
            if (geometry.has("error") && geometry["error"] != renderer.get("geometry_error", "")) {
                Dictionary diagnostic;
                Ref<Resource> source_emitter = plan["source"];
                diagnostic["severity"] = "error";
                diagnostic["emitter"] = source_emitter->get("display_name");
                diagnostic["message"] = geometry["error"];
                diagnostics.append(diagnostic);
            }
            renderer["geometry_error"] = geometry.get("error", "");
            Array arrays = geometry["arrays"];
            if (arrays.is_empty())
                continue;
            PackedVector3Array positions = arrays[Mesh::ARRAY_VERTEX];
            for (int64_t index = 0; index < positions.size(); ++index)
                bounds = bounds ? bounds->expand(positions[index]) : AABB(positions[index], Vector3());
            if (visible) {
                NBGeometryRecord record;
                record.arrays = arrays;

                record.per_vertex = true;
                record.geometry_colors = true;
                record.context_slot = state["context_slot"];
                record.relative_times = geometry["relative_times"];
                records.push_back(std::move(record));
            }
            continue;
        }
        particles=sorted_particles(filtered,source,camera,space);
        String offset_name = packet["offset_name"];
        bool fixed_offset = !offset_name.begins_with("Particles.");
        double shared_offset =
            fixed_offset ? camera_offset(offset_name, Dictionary(), emitter, frame, builtins) : 0.;
        Dictionary binding = renderer.get("mesh_binding", Dictionary());
        Ref<Resource> entry = binding.get("entry", Variant());
        if (kind == 3 && entry.is_null()) {
            Dictionary diagnostic;
            diagnostic["severity"] = "error";
            Ref<Resource> emitter_source = plan["source"];
            diagnostic["emitter"] = emitter_source->get("display_name");
            diagnostic["message"] = "Mesh renderer has no valid compiled entry.";
            diagnostics.append(diagnostic);
            continue;
        }
        NBSpriteFrame sprite_frame;
        if (kind != 3) sprite_frame = prepare_sprite_frame(renderer, camera, space);
        for (int index = 0; index < particles.size(); ++index) {
            Dictionary p = particles[index];
            if (kind == 3 && !mesh_selected(renderer, p))
                continue;
            double offset = fixed_offset && !bool(p.get("_emitter_source", false))
                                ? shared_offset
                                : camera_offset(offset_name, p, emitter, frame, builtins);
            Transform3D transform;
            Color color = p["color"], attributes;
            if (kind == 3) {
                transform = mesh_transform(entry, p, local, owner, camera, offset,
                                           binding.get("facing", Dictionary()));
                attributes = Color(p["subimage_index"], p["material_random"], 1, 1);
            } else {
                const NBSpriteRow row = sprite_row(sprite_frame, p);
                const auto footprint =
                    coverage_sample(renderer.get("pixel_coverage", Dictionary()), row.size,
                             space.xform(NBNativeValues::unreal_to_godot(row.position)), camera);
                transform = sprite_transform_row(sprite_frame, row, offset, footprint.size_ratio);
                color *= footprint.color_scale;
                Vector2 size = row.size;
                attributes = Color(p["subimage_index"], p["material_random"],
                                   row.uv_x * (size.x < 0 ? -1. : 1.),
                                   row.uv_y * (size.y < 0 ? -1. : 1.));
            }
            merge(transform.xform(AABB(renderer["mesh_aabb"])));
            if (!visible)
                continue;
            NBGeometryRecord record;
            record.transform = transform;
            record.color = color;
            record.dynamic = p["dynamic"];
            record.dynamic1 = p["dynamic1"];
            record.dynamic2 = p["dynamic2"];
            record.dynamic3 = p["dynamic3"];
            record.context_slot = state["context_slot"];
            record.attributes = attributes;
            record.relative_time = p["normalized_age"];
            if (kind == 3) {
                record.arrays = Array(state["arrays"]);

                record.geometry_colors = true;
            }
            records.push_back(std::move(record));
        }
    }
    if (bounds) result.bounds = *bounds;
    mark("fallback_tail");
    if(profile_host) profile_host->set_meta("angara_fx_geometry_timings_usec",phases);
    return result;
}
Array NBNativeRendererGeometry::packet_records(const std::vector<NBGeometryRecord> &records) {
    TypedArray<Dictionary> result;
    for (const auto &record : records) {
        Dictionary row;
        // Preserve the public adapter's historical field presence/key order.
        if (record.per_vertex) {
            row["arrays"] = *record.arrays;
            row["per_vertex_attributes"] = true;
            row["geometry_colors"] = record.geometry_colors;
            row["transform"] = record.transform;
            row["context_slot"] = record.context_slot;
            row["attributes"] = record.attributes;
            row["color"] = record.color;
            row["relative_times"] = record.relative_times;
        } else {
            row["transform"] = record.transform;
            row["color"] = record.color;
            row["dynamic"] = record.dynamic;
            row["dynamic1"] = record.dynamic1;
            row["dynamic2"] = record.dynamic2;
            row["dynamic3"] = record.dynamic3;
            row["context_slot"] = record.context_slot;
            row["attributes"] = record.attributes;
            row["relative_time"] = record.relative_time;
            if (record.arrays) {
                row["arrays"] = *record.arrays;
                row["geometry_colors"] = record.geometry_colors;
            }
        }
        result.append(row);
    }
    return result;
}
Dictionary NBNativeRendererGeometry::composite_records(const Array &states, Camera3D *camera,
                                                        const Transform3D &owner, const Dictionary &frame,
                                                        const Dictionary &builtins, int64_t tick) {
    NBGeometryPacket packet = composite_packet(states, camera, owner, frame, builtins, tick);
    Dictionary result;
    result["records"] = packet_records(packet.records);
    result["bounds"] = packet.bounds;
    result["diagnostics"] = packet.diagnostics;
    return result;
}
} // namespace godot
