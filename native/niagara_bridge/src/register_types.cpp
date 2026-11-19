#include "workspace_document.h"
#include "editor_actions.h"
#include "editor_operations.h"
#include "scene_context.h"
#include "editor_models.h"
#include "material_programs.h"
#include "material_authoring.h"
#include "raster_material_program.h"
#include "material_composer.h"
#include "material_derived.h"
#include "material_compiler.h"
#include "material_graph_loader.h"
#include "texture_builder.h"
#include "native_effect.h"
#include "native_player.h"
#include "vm_program.h"
#include "vm_datasets.h"
#include "collision_world.h"
#include "vm_events.h"
#include "composite_renderer.h"
#include "raster_buffers.h"
#include "particle_buffer.h"
#include "particle_step.h"
#include "world_clock.h"
#include "ribbon_geometry.h"
#include "runtime_control.h"
#include "text_saver.h"
#include "curve_evaluator.h"
#include "value_evaluator.h"
#include "spawn_scheduler.h"
#include "particle_context.h"
#include "gpu_buffers.h"
#include "gpu_backend.h"
#include "attribute_values.h"
#include "user_parameters.h"
#include "runtime_operation.h"
#include "renderer_filter.h"
#include "module_math.h"
#include "material_exposure.h"
#include "module_execution.h"
#include "particle_forces.h"
#include "mesh_orientation.h"
#include "torus_geometry.h"
#include "renderer_geometry.h"
#include "particle_lights.h"
#include "component_bounds.h"
#include "effect_controller.h"
#include "raster_pass.h"
#include "lighting_volume.h"
#include "geometry_cache.h"
#include "render_policy.h"
#include "compiler_frontend.h"
#include "effect_compiler.h"
#include "source_program.h"
#include "gpu_program.h"
#include "source_reader.h"
#include "rng_contract.h"
#include "document_copy.h"
#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;
static void initialize_native_bridge(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(NBNativeParticleEffect);
    GDREGISTER_CLASS(NBNativeRasterPass);
    GDREGISTER_CLASS(NBNativeLightingVolume);
    GDREGISTER_CLASS(NBNativeGeometryCache);
    GDREGISTER_CLASS(NBNativeRenderPolicy);
    GDREGISTER_CLASS(NBNativeCompilerAttributes);
    GDREGISTER_CLASS(NBNativeProgramSchema);
    GDREGISTER_CLASS(NBNativeEffectCompiler);
    GDREGISTER_CLASS(NBNativeSourceProgram);
    GDREGISTER_CLASS(NBNativeGPUProgram);
    GDREGISTER_CLASS(NBNativeSourceReader);
    GDREGISTER_CLASS(NBNativeExecutionSource);
    GDREGISTER_CLASS(NBNativeLegacyRNGShape);
    GDREGISTER_CLASS(NBNativeRNGContract);
    GDREGISTER_CLASS(NBNativeDocumentCopy);
    GDREGISTER_CLASS(NBNativeTextureBuilder);
    GDREGISTER_CLASS(NBNativeT3DParser);
    GDREGISTER_CLASS(NBNativeMaterialGraphLoader);
    GDREGISTER_CLASS(NBNativeMaterialCompiler);
    GDREGISTER_CLASS(NBNativeMaterialPrograms);
    GDREGISTER_CLASS(NBNativeMaterialAuthoring);
    GDREGISTER_CLASS(NBNativeRasterProgram);
    GDREGISTER_CLASS(NBNativeMaterialComposer);
    GDREGISTER_CLASS(NBNativeMaterialDerived);
    GDREGISTER_CLASS(NBNativeSceneContext);
    GDREGISTER_CLASS(NBNativeEditorModels);
    GDREGISTER_CLASS(NBNativeEditorOperations);
    GDREGISTER_CLASS(NBNativeEditorActions);
    GDREGISTER_CLASS(NBNativeWorkspaceDocument);
    GDREGISTER_CLASS(NBNativeCustomArithmetic);
    GDREGISTER_CLASS(NBNativeParticlePlayer);
    GDREGISTER_CLASS(NBNativeVMRandom);
    GDREGISTER_CLASS(NBNativeVMIDs);
    GDREGISTER_CLASS(NBNativeVMReaders);
    GDREGISTER_CLASS(NBNativeVMEvents);
    GDREGISTER_CLASS(NBNativeCollisionWorld);
    GDREGISTER_CLASS(NBNativeVMProgram);
    GDREGISTER_CLASS(NBNativeVMExecutor);
    GDREGISTER_CLASS(NBNativeVMDataset);
    GDREGISTER_CLASS(NBNativeVMParticles);
    GDREGISTER_CLASS(NBNativeParticleBuffer);
    GDREGISTER_CLASS(NBNativeVMParticleStep);
    GDREGISTER_CLASS(NBNativeWorldClock);
    GDREGISTER_CLASS(NBNativeRibbonGeometry);
    GDREGISTER_CLASS(NBNativeSourceClock);
    GDREGISTER_CLASS(NBNativeDocument);
    GDREGISTER_CLASS(NBNativeLifecycle);
    GDREGISTER_CLASS(NBNativeTextSaver);
    GDREGISTER_CLASS(NBNativeCurves);
    GDREGISTER_CLASS(NBNativeNiagaraRNG);
    GDREGISTER_CLASS(NBNativeSourceRandom);
    GDREGISTER_CLASS(NBNativeDynamicMath);
    GDREGISTER_CLASS(NBNativeValues);
    GDREGISTER_CLASS(NBNativeSpawnScheduler);
    GDREGISTER_CLASS(NBNativeParticleContext);
    GDREGISTER_CLASS(NBNativeGPUBuffers);
    GDREGISTER_CLASS(NBNativeVectorFieldGPU);
    GDREGISTER_CLASS(NBNativeGPUBackend);
    GDREGISTER_INTERNAL_CLASS(NBNativeGridVolumes);
    GDREGISTER_CLASS(NBNativeParticleView);
    GDREGISTER_CLASS(NBNativeGPUParticleBuffer);
    GDREGISTER_CLASS(NBNativeAttributeValues);
    GDREGISTER_CLASS(NBNativeUserParameters);
    GDREGISTER_CLASS(NBNativeRendererViews);
    GDREGISTER_CLASS(NBNativeRuntimeOperations);
    GDREGISTER_CLASS(NBNativeRendererFilter);
    GDREGISTER_CLASS(NBNativeCPUMath);
    GDREGISTER_CLASS(NBNativeLinearColor);
    GDREGISTER_CLASS(NBNativeMaterialExposure);
    GDREGISTER_CLASS(NBNativeParticleModules);
    GDREGISTER_CLASS(NBNativeVectorFields);
    GDREGISTER_CLASS(NBNativeAttractionForces);
    GDREGISTER_CLASS(NBNativeCurlNoise);
    GDREGISTER_CLASS(NBNativeMeshOrientation);
    GDREGISTER_CLASS(NBNativeMeshFacing);
    GDREGISTER_CLASS(NBNativeTorusGeometry);
    GDREGISTER_CLASS(NBNativeSpriteFacing);
    GDREGISTER_CLASS(NBNativeRendererGeometry);
    GDREGISTER_CLASS(NBNativeParticleLights);
    GDREGISTER_CLASS(NBNativeComponentBounds);
    GDREGISTER_CLASS(NBNativeEffectController);
    GDREGISTER_CLASS(NBNativeVMSystem);
    GDREGISTER_CLASS(NBNativeVMParameters);
    GDREGISTER_CLASS(NBNativeCompositeRenderer);
    GDREGISTER_CLASS(NBNativeEffectRevision);
    GDREGISTER_CLASS(NBNativeRasterBuffers);
}
static void uninitialize_native_bridge(ModuleInitializationLevel level) {
    if (level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        NBNativeGeometryCache::shutdown();
        NBNativeMaterialPrograms::clear_cache();
        NBNativeCurlNoise::clear_cache();
        NBNativeCurves::clear_cache();
    }
}

extern "C" GDExtensionBool GDE_EXPORT niagara_bridge_native_init(
        GDExtensionInterfaceGetProcAddress get_proc_address,
        GDExtensionClassLibraryPtr library,
        GDExtensionInitialization *initialization) {
    GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
    init.register_initializer(initialize_native_bridge);
    init.register_terminator(uninitialize_native_bridge);
    init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
