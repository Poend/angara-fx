#include "renderer_rows.h"
#include "attribute_values.h"
#include "renderer_filter.h"
#include "particle_context.h"
#include "value_evaluator.h"
#include <algorithm>
#include <godot_cpp/variant/utility_functions.hpp>
namespace godot {
Variant NBRendererRows::field(const NBRendererRow &row, int64_t handle, const Variant &fallback) const {
    if (handle>=0 && size_t(handle)<slots.size() && slots[size_t(handle)]>=0)
        return values[row.overlay_offset+size_t(slots[size_t(handle)])];
    return source->has_field(handle,row.lane) ? source->read(handle,row.lane) : fallback;
}
Variant NBRendererRows::attribute(const NBRendererRow &row, int64_t handle, const Variant &fallback) const {
    if (handle>=0 && size_t(handle)<slots.size() && slots[size_t(handle)]>=0)
        return values[row.overlay_offset+size_t(slots[size_t(handle)])];
    return source->attribute_or(handle,row.lane,fallback);
}
Variant NBRendererRows::special(const NBRendererRow &row,int64_t slot,const Variant &fallback) const {
    return slot>=0 ? values[row.overlay_offset+size_t(slot)] : fallback;
}
Dictionary NBRendererRows::overrides(const NBRendererRow &row) const {
    Dictionary result,attributes;
    for(size_t i=0;i<writes.size();++i) {
        const auto &write=writes[i];const auto &value=values[row.overlay_offset+i];
        if(write.attribute) {attributes[write.key]=value;result["attributes"]=attributes;}
        else result[write.key]=value;
    }
    return result;
}
NBRendererRows NBNativeRendererViews::resident_rows(Dictionary emitter, const Ref<Resource> &renderer,
                                                    const Dictionary &parameters, const Vector3 &global_position,
                                                    const Dictionary &frame, const Dictionary &builtins) {
    NBRendererRows result;
    result.source = NBNativeParticleView::for_emitter(emitter);
    if (result.source.is_null()) return result;
    // A completed emitter may own an uninitialized zero-lane buffer. No field
    // reads occur for it; retain the ordinary empty-view validation behavior.
    if(result.source->get_count()>0) result.errors.append_array(result.source->get_errors());
    Dictionary plan=emitter["plan"], filters=plan.get("renderer_filters",Dictionary()),
               layouts=plan.get("renderer_attributes",Dictionary()), config=filters.get(renderer,Dictionary()),
               attributes=layouts.get(renderer,Dictionary());
    if (bool(attributes.get("emitter_source",false))) { result.source.unref(); return result; }
    Vector3 position=bool(plan["local_space"]) ? Vector3() : NBNativeValues::godot_to_unreal(global_position);
    Dictionary values=parameters.duplicate();
    String enabled=config.get("enabled_name","None");
    if ((!enabled.is_empty() && enabled!="None") || bool(config.get("host_tag",false)) || bool(attributes.get("host_bound",false)))
        values.merge(NBNativeParticleContext::for_emitter(emitter,0.,frame.get("simulation_time",0.),frame),true);
    // Reuse host validation and rejection semantics without materializing lanes.
    Dictionary host=config.duplicate(); host["particle_tag"]=false;
    Dictionary filtered=NBNativeRendererFilter::apply(host,Array(),values);
    result.errors.append_array(filtered["errors"]);
    if (!result.errors.is_empty() || !bool(filtered["all_visible"])) return result;
    bool tagged=config.get("particle_tag",false);
    String tag_name=config.get("tag_name","None");
    int64_t tag_handle=tagged ? result.source->attribute_handle(tag_name) : -1;
    // Filter all lanes before projection, matching error precedence.
    for (int64_t lane=0;lane<result.source->get_count();++lane) {
        if (tagged) {
            Variant value=result.source->read(tag_handle,lane);
            if (!NBNativeRendererFilter::int32(value)) {
                result.errors.append("Missing/invalid particle visibility value: "+tag_name);result.rows.clear();return result;
            }
            if (int64_t(value)!=int64_t(config.get("tag",0))) continue;
        }
        NBRendererRow row;row.lane=lane;result.rows.push_back(std::move(row));
    }
    Array fields=attributes.get("fields",Array());
    if (fields.is_empty()) return result;
    result.errors.append_array(attributes.get("errors",Array()));
    if (!result.errors.is_empty()) {result.rows.clear();return result;}
    if(result.rows.empty()) return result;
    struct Binding {
        String key,name,type,kind;
        Variant fallback;
        int64_t handle=-1,first=-1,second=-1,flag=-1,velocity=-1;
        bool velocity_flag=false;
        int64_t source_velocity=-1;
        bool uv=false,rotation=false,dynamic=false,facing=false;
    };
    std::vector<Binding> bindings;
    Dictionary field_slots,attribute_slots;
    auto slot=[&](const Variant &key,bool attribute) {
        Dictionary &map=attribute ? attribute_slots : field_slots;
        if(map.has(key)) return int64_t(map[key]);
        int64_t index=int64_t(result.writes.size());map[key]=index;
        result.writes.push_back({key,attribute});
        int64_t handle=attribute ? result.source->attribute_handle(String(key)) : result.source->field_handle(String(key));
        if(size_t(handle)>=result.slots.size()) result.slots.resize(size_t(handle)+1,-1);
        result.slots[size_t(handle)]=index;
        return index;
    };
    for (int64_t i=0;i<fields.size();++i) {
        Dictionary field=fields[i];Binding binding;
        binding.key=field["key"];binding.name=field["name"];binding.type=field["type"];
        String canonical=field["canonical"];binding.kind=field.get("kind","");
        binding.fallback=field.get("fallback",fallback(binding.key,position));
        if (binding.kind=="particle") binding.handle=result.source->binding_handle(binding.name,builtins);
        binding.uv=canonical=="Particles.UVScale";
        if(binding.uv) {binding.first=slot("uv_flip",false);binding.second=slot("uv_flip_y",false);}
        else if(builtins.has(canonical)) {
            binding.rotation=canonical=="Particles.SpriteRotation";
            binding.dynamic=canonical.begins_with("Particles.DynamicMaterialParameter");
            Array destination=builtins[canonical];
            if(destination.size()>1) binding.first=slot(destination[1],false);
        } else binding.first=slot(canonical,true);
        if(binding.key=="Velocity") {
            binding.velocity_flag=true;
            binding.velocity=field_slots.get("velocity",-1);
            if(binding.velocity<0) binding.source_velocity=result.source->field_handle("velocity");
            binding.flag=result.velocity_slot=slot("_renderer_velocity",false);
        } else if(binding.key=="SpriteFacing" || binding.key=="SpriteAlignment") {
            binding.facing=true;binding.flag=slot(String("_renderer_")+binding.key,false);
            if(binding.key=="SpriteFacing") result.facing_slot=binding.flag;
            else result.alignment_slot=binding.flag;
        }
        bindings.push_back(std::move(binding));
    }
    result.values.resize(result.rows.size()*result.writes.size());
    for(size_t i=0;i<result.rows.size();++i) {
        auto &row=result.rows[i];row.overlay_offset=i*result.writes.size();
        for (const auto &binding:bindings) {
            Variant value=binding.kind=="particle" ? result.source->read(binding.handle,row.lane)
                             : binding.kind=="host" ? values.get(binding.name,binding.fallback) : binding.fallback;
            if (!valid(value,binding.type)) {
                result.errors.append("Missing/invalid renderer "+binding.key+" value: "+binding.name);
                result.rows.clear();return result;
            }
            Variant typed=NBNativeAttributeValues::coerce(value,binding.type);
            if(binding.uv) {
                Vector2 uv=typed;result.values[row.overlay_offset+size_t(binding.first)]=uv.x;
                result.values[row.overlay_offset+size_t(binding.second)]=uv.y;
            } else if(binding.first>=0) {
                if(binding.rotation) typed=Math::deg_to_rad(double(typed));
                if(binding.dynamic) typed=NBNativeAttributeValues::coerce(typed,"color");
                result.values[row.overlay_offset+size_t(binding.first)]=typed;
            }
            if(binding.velocity_flag)
                result.values[row.overlay_offset+size_t(binding.flag)]=binding.velocity>=0
                    ? result.values[row.overlay_offset+size_t(binding.velocity)]
                    : result.source->read(binding.source_velocity,row.lane);
            else if(binding.facing)
                result.values[row.overlay_offset+size_t(binding.flag)]=NBNativeAttributeValues::coerce(value,"vec3");
        }
    }
    return result;
}
void NBRendererRows::sort(const Ref<Resource> &renderer,Camera3D *camera,const Transform3D &space) {
    String mode=NBNativeValues::enum_name(NBNativeRendererFilter::input(renderer,"SortMode","None"));
    if (mode=="None" || rows.size()<2) return;
    if (bool(NBNativeRendererFilter::input(renderer,"bSortOnlyWhenTranslucent",false))) {
        Variant translucent=NBNativeRendererFilter::input(renderer,"MaterialTranslucent",Variant());
        Ref<Resource> material=renderer->get("material");
        if (translucent==Variant(false) || (translucent.get_type()==Variant::NIL && material.is_valid() &&
            material->is_class("BaseMaterial3D") && int64_t(material->get("transparency"))==0)) return;
    }
    bool spatial=mode=="ViewDepth" || mode=="ViewDistance";
    if (spatial && !camera) return;
    String attribute="Particles.NormalizedAge";
    Array bindings=renderer->get("bindings");
    for (int64_t i=0;i<bindings.size();++i) {
        Dictionary b=bindings[i];
        if (bool(b.get("enabled",true)) && b.get("name","")==Variant("CustomSorting"))
            attribute=NBNativeValues::enum_name(b.get("value",attribute));
    }
    int64_t position=source->field_handle("position"), id=source->field_handle("id"),
            custom=source->field_handle(attribute=="Particles.Age" ? "age" : attribute=="Particles.Lifetime" ? "lifetime" : attribute=="Particles.ID" ? "id" : "normalized_age");
    struct Key {NBRendererRow row;double key;int64_t id;};std::vector<Key> keyed;keyed.reserve(rows.size());
    for (auto &row:rows) {
        double key=0.;
        if (spatial) {
            Vector3 p=space.xform(NBNativeValues::unreal_to_godot(field(row,position))), difference=p-camera->get_global_position();
            key=mode=="ViewDistance" ? p.distance_squared_to(camera->get_global_position()) : -camera->get_global_basis().get_column(2).dot(difference);
        } else key=source->read(custom,row.lane);
        keyed.push_back({std::move(row),key,source->read(id,row.lane)});
    }
    bool ascending=mode=="CustomAscending";
    std::stable_sort(keyed.begin(),keyed.end(),[ascending](const Key&a,const Key&b){
        if (UtilityFunctions::is_equal_approx(a.key,b.key)) return a.id<b.id;
        return ascending ? a.key<b.key : a.key>b.key;
    });
    rows.clear();rows.reserve(keyed.size());for(auto &key:keyed) rows.push_back(std::move(key.row));
}
Dictionary NBNativeRendererViews::resident_projection(Dictionary emitter,const Ref<Resource> &renderer,
                                                       const Dictionary &parameters,const Vector3 &global_position,
                                                       const Dictionary &frame,const Dictionary &builtins,
                                                       Camera3D *camera,const Transform3D &space) {
    NBNativeParticleView::materialize(emitter);
    auto view=resident_rows(emitter,renderer,parameters,global_position,frame,builtins);
    Dictionary result;Array output,indices;result["particles"]=output;result["indices"]=indices;
    result["errors"]=view.errors;result["supported"]=view.source.is_valid();
    if(view.source.is_null() || !view.errors.is_empty()) return result;
    view.sort(renderer,camera,space);
    Dictionary layouts=Dictionary(emitter["plan"]).get("renderer_attributes",Dictionary());
    Array fields=Dictionary(layouts.get(renderer,Dictionary())).get("fields",Array());
    Array particles=emitter["particles"];
    for(const auto &row:view.rows) {
        Dictionary original=particles[row.lane];
        if(fields.is_empty()) output.append(original);
        else {
            Dictionary p=original.duplicate();p["_renderer_source"]=original;
            Dictionary attributes=Dictionary(original.get("attributes",Dictionary())).duplicate();
            Dictionary overlay=view.overrides(row);
            attributes.merge(overlay.get("attributes",Dictionary()),true);
            p.merge(overlay,true);p["attributes"]=attributes;output.append(p);
        }
        indices.append(row.lane);
    }
    return result;
}
} // namespace godot
