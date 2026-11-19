#include "renderer_geometry.h"
#include "attribute_values.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <algorithm>
#include <array>
#include <cstring>
namespace godot {
Dictionary NBNativeRendererGeometry::synchronize_resident(Dictionary renderer,Dictionary emitter,Camera3D *camera,
                                                           const Transform3D &owner,const Dictionary &frame,
                                                           const Dictionary &builtins,const Dictionary &parameters,
                                                           const String &offset_name,bool mesh) {
    Ref<Resource> source=renderer["source"];
    NBRendererRows rows=NBNativeRendererViews::resident_rows(emitter,source,parameters,owner.origin,frame,builtins);
    Dictionary result;result["supported"]=rows.source.is_valid();result["errors"]=rows.errors;
    result["particles"]=rows.rows.size();
    if(rows.source.is_null()) return result;
    Object *raw=renderer.get("instance",Variant());auto *instance=Object::cast_to<MultiMeshInstance3D>(raw);
    if(!instance || instance->get_multimesh().is_null()) return result;
    Ref<MultiMesh> multimesh=instance->get_multimesh();
    if(!rows.errors.is_empty()) {multimesh->set_visible_instance_count(0);return result;}
    Dictionary plan=emitter["plan"],binding=renderer.get("mesh_binding",Dictionary());
    bool local=plan["local_space"];Transform3D space=local ? owner : Transform3D();
    const int64_t requested_count=rows.rows.size();
    int64_t mesh_index=rows.source->field_handle("mesh_index");
    if(mesh) {
        const int64_t selected=binding.get("source_index",0);
        rows.rows.erase(std::remove_if(rows.rows.begin(),rows.rows.end(),[&](const NBRendererRow &row){
            return int64_t(rows.field(row,mesh_index))!=selected;
        }),rows.rows.end());
    }
    rows.sort(source,camera,space);
    result["visible_particles"]=rows.rows.size();
    const int64_t count=rows.rows.size();
    if(requested_count>int64_t(renderer["capacity"])) {
        int64_t capacity=std::max(int64_t(16),requested_count*2);renderer["capacity"]=capacity;
        multimesh->set_instance_count(capacity);int64_t width=std::min(capacity*5,int64_t(1024)),height=(capacity*5+width-1)/width;
        Ref<Image> image=Image::create_empty(width,height,false,Image::FORMAT_RGBAF);renderer["attributes_image"]=image;
        Ref<ImageTexture> texture=renderer["attributes_texture"];texture->set_image(image);
    }
    multimesh->set_visible_instance_count(count);
    Ref<Image> image=renderer["attributes_image"];AABB bounds,mesh_bounds=renderer["mesh_aabb"];
    Ref<Resource> entry=binding.get("entry",Variant());Dictionary facing=binding.get("facing",Dictionary());
    if(mesh && entry.is_null()) {rows.errors.append("Mesh renderer has no valid compiled entry.");multimesh->set_visible_instance_count(0);return result;}
    const char *names[]={"position","velocity","mesh_scale","mesh_orientation","color","subimage_index","material_random","normalized_age","dynamic","dynamic1","dynamic2","dynamic3"};
    int64_t handles[12];for(int i=0;i<12;++i) handles[i]=rows.source->field_handle(names[i]);
    bool fixed=!offset_name.begins_with("Particles.");
    double shared=fixed ? camera_offset(offset_name,Dictionary(),emitter,frame,builtins) : 0.;int64_t offset=-1;
    if(!fixed) {
        if(offset_name=="Particles.SpriteRotation") offset=rows.source->binding_handle(offset_name,builtins);
        else if(builtins.has(offset_name)) offset=rows.source->field_handle(Array(builtins[offset_name])[1]);
        else offset=rows.source->attribute_handle(offset_name);
    }
    NBSpriteFrame sprite_frame;NBSpriteColumns sprite_columns_value;
    if(!mesh) {sprite_frame=prepare_sprite_frame(renderer,camera,space);sprite_columns_value=sprite_columns(sprite_frame,rows.source);}
    const auto coverage_frame=prepare_coverage(renderer.get("pixel_coverage",Dictionary()),camera);
    // Godot's 3D MultiMesh ABI is three row-major vec4 transform rows,
    // followed by optional float32 color and custom-data vec4s. Retain the
    // existing tail: callers may lower visible_count without clearing slots.
    const bool bulk=multimesh->get_transform_format()==MultiMesh::TRANSFORM_3D;
    const bool colors=multimesh->is_using_colors(),custom=multimesh->is_using_custom_data();
    const int64_t stride=12+(colors ? 4 : 0)+(custom ? 4 : 0);
    PackedFloat32Array instances;
    float *instance_data=nullptr;
    if(bulk && count) {
        instances=multimesh->get_buffer();
        if(instances.size()!=multimesh->get_instance_count()*stride) instances.resize(multimesh->get_instance_count()*stride);
        instance_data=instances.ptrw();
    }
    const bool bulk_image=image.is_valid() && image->get_format()==Image::FORMAT_RGBAF && !image->has_mipmaps();
    PackedByteArray pixels;uint8_t *pixel_data=nullptr;
    if(bulk_image && count) {pixels=image->get_data();pixel_data=pixels.ptrw();}
    int64_t index=0;
    for(const auto &row:rows.rows) {
        auto value=[&](int i){return rows.field(row,handles[i]);};
        double camera_offset_value=fixed ? shared : double(rows.source->read(offset,row.lane));
        Transform3D transform;Color color=value(4),attributes;
        if(mesh) {
            NBMeshRow particle;particle.position=value(0);particle.velocity=value(1);particle.scale=value(2);particle.orientation=value(3);
            transform=mesh_transform_row(entry,particle,local,owner,camera,camera_offset_value,facing);
            attributes=Color(value(5),value(6),1,1);
        } else {
            const auto particle=sprite_renderer_row(sprite_frame,sprite_columns_value,rows,row);
            const auto footprint=coverage_prepared(coverage_frame,particle.size,space.xform(NBNativeValues::unreal_to_godot(particle.position)));
            transform=sprite_transform_row(sprite_frame,particle,camera_offset_value,footprint.size_ratio);color*=footprint.color_scale;
            attributes=Color(value(5),value(6),particle.uv_x*(particle.size.x<0 ? -1. : 1.),particle.uv_y*(particle.size.y<0 ? -1. : 1.));
        }
        Color dynamic=value(8);
        if(instance_data) {
            float *out=instance_data+index*stride;
            for(int axis=0;axis<3;++axis) {
                for(int column=0;column<3;++column) out[axis*4+column]=float(transform.basis.rows[axis][column]);
                out[axis*4+3]=float(transform.origin[axis]);
            }
            int cursor=12;
            for(const auto &channel : {std::pair<bool,Color>(colors,color),std::pair<bool,Color>(custom,dynamic)}) {
                if(!channel.first) continue;
                for(int component=0;component<4;++component) out[cursor++]=float(channel.second[component]);
            }
        } else {
            multimesh->set_instance_transform(index,transform);multimesh->set_instance_color(index,color);multimesh->set_instance_custom_data(index,dynamic);
        }
        AABB current=transform.xform(mesh_bounds);bounds=index==0 ? current : bounds.merge(current);
        if(image.is_valid()) {
            Color values[]={attributes,Color(value(9)),Color(value(10)),Color(value(11)),Color(value(7),0,0,0)};
            int64_t width=image->get_width();
            for(int field=0;field<5;++field) {
                int64_t pixel=index*5+field;
                if(pixel_data && pixel*16+16<=pixels.size()) {
                    std::array<float,4> rgba;for(int component=0;component<4;++component) rgba[component]=float(values[field][component]);
                    std::memcpy(pixel_data+pixel*16,rgba.data(),16);
                } else image->set_pixel(pixel%width,pixel/width,values[field]);
            }
        }
        ++index;
    }
    if(instance_data) multimesh->set_buffer(instances);
    if(pixel_data) image->set_data(image->get_width(),image->get_height(),false,Image::FORMAT_RGBAF,pixels);
    multimesh->set_custom_aabb(bounds.grow(.01));
    if(count) {Ref<ImageTexture> texture=renderer["attributes_texture"];if(texture.is_valid()) texture->update(image);}
    return result;
}
} // namespace godot
