cmake_minimum_required(VERSION 3.24)
include("${CMAKE_CURRENT_LIST_DIR}/version.cmake")
project(angara_fx VERSION "${ANGARA_FX_VERSION}" LANGUAGES CXX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ANGARA_FX_MANIFEST}")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Use an out-of-source build directory.")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/.gdignore" "")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(GODOTCPP_API_VERSION "4.7" CACHE STRING "Godot API")
set(GODOTCPP_TARGET "template_debug" CACHE STRING "Godot target")
set(GODOTCPP_BUILD_PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/build_profile.json" CACHE FILEPATH "Godot classes used by the runtime and editor")
include(FetchContent)
# godot-cpp 10.0.0-stable, Godot 4.7 single-precision API.
FetchContent_Declare(godot_cpp
  GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
  GIT_TAG 507ed9d840c01a3c5b2a39af8bb4000bfac30bf5
)
FetchContent_MakeAvailable(godot_cpp)
add_library(niagara_bridge_native SHARED src/native_effect.cpp src/native_player.cpp src/vm_executor.cpp src/vm_state.cpp src/vm_external.cpp src/vm_readers.cpp src/vm_datasets.cpp src/vm_system.cpp src/vm_spawn.cpp src/vm_events.cpp src/collision_world.cpp src/composite_renderer.cpp src/raster_buffers.cpp src/particle_buffer.cpp src/particle_step.cpp src/world_clock.cpp src/ribbon_geometry.cpp src/runtime_control.cpp src/text_saver.cpp src/curve_evaluator.cpp src/value_evaluator.cpp src/spawn_scheduler.cpp src/particle_context.cpp src/gpu_buffers.cpp src/gpu_backend.cpp src/attribute_values.cpp src/runtime_operation.cpp src/renderer_filter.cpp src/module_math.cpp src/material_exposure.cpp src/module_execution.cpp src/particle_forces.cpp src/mesh_orientation.cpp src/torus_geometry.cpp src/module_spawn.cpp src/register_types.cpp)
target_link_libraries(niagara_bridge_native PRIVATE godot-cpp)
target_sources(niagara_bridge_native PRIVATE src/module_pipeline.cpp)
target_sources(niagara_bridge_native PRIVATE src/renderer_geometry.cpp)
target_sources(niagara_bridge_native PRIVATE src/composite_geometry.cpp)
target_sources(niagara_bridge_native PRIVATE src/particle_lights.cpp)
target_sources(niagara_bridge_native PRIVATE src/component_bounds.cpp)
target_sources(niagara_bridge_native PRIVATE src/spawn_batches.cpp)
target_sources(niagara_bridge_native PRIVATE src/effect_controller.cpp)
target_sources(niagara_bridge_native PRIVATE src/effect_backends.cpp)
target_sources(niagara_bridge_native PRIVATE src/gpu_particle_reader.cpp)
target_sources(niagara_bridge_native PRIVATE src/effect_clock.cpp)
target_sources(niagara_bridge_native PRIVATE src/effect_restart.cpp)
target_sources(niagara_bridge_native PRIVATE src/effect_parameters.cpp src/effect_bounds.cpp)
target_sources(niagara_bridge_native PRIVATE src/renderer_factory.cpp)
target_sources(niagara_bridge_native PRIVATE src/renderer_materials.cpp)
target_sources(niagara_bridge_native PRIVATE src/renderer_ribbons.cpp src/renderer_player.cpp)
target_sources(niagara_bridge_native PRIVATE src/renderer_composite.cpp)
target_sources(niagara_bridge_native PRIVATE src/renderer_passes.cpp)
target_sources(niagara_bridge_native PRIVATE src/renderer_refresh.cpp)
target_sources(niagara_bridge_native PRIVATE src/raster_pass.cpp src/raster_submission.cpp src/raster_render.cpp)
target_sources(niagara_bridge_native PRIVATE src/lighting_volume.cpp src/geometry_cache.cpp src/render_policy.cpp)
target_sources(niagara_bridge_native PRIVATE src/compiler_frontend.cpp src/program_schema.cpp)
target_sources(niagara_bridge_native PRIVATE src/effect_compiler.cpp src/effect_compiler_values.cpp src/effect_compiler_settings.cpp src/effect_compiler_mesh.cpp src/effect_compiler_modules.cpp src/effect_compiler_renderers.cpp)
target_sources(niagara_bridge_native PRIVATE src/source_program.cpp src/source_program_bindings.cpp src/source_program_curves.cpp src/source_program_lifecycle.cpp src/source_program_modules.cpp)
target_sources(niagara_bridge_native PRIVATE src/gpu_program.cpp src/gpu_program_expressions.cpp src/gpu_program_curves.cpp src/gpu_program_rng.cpp src/gpu_program_geometry.cpp src/gpu_program_initialize.cpp src/gpu_program_locations.cpp src/gpu_program_orientation.cpp src/gpu_program_modules.cpp)
target_sources(niagara_bridge_native PRIVATE src/compiler_policy.cpp src/source_reader.cpp src/execution_source.cpp)
target_sources(niagara_bridge_native PRIVATE src/legacy_rng_shape.cpp src/rng_contract.cpp src/rng_contract_helpers.cpp src/rng_contract_walk.cpp)
target_compile_definitions(niagara_bridge_native PRIVATE NOMINMAX)
if(MSVC)
  target_compile_options(niagara_bridge_native PRIVATE /W4 /fp:precise)
endif()
set_target_properties(niagara_bridge_native PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/../../addons/niagara_bridge/native/bin>"
  LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/../../addons/niagara_bridge/native/bin>"
  PDB_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/symbols>"
  OUTPUT_NAME "niagara_bridge_native")

target_sources(niagara_bridge_native PRIVATE src/user_parameters.cpp)
target_sources(niagara_bridge_native PRIVATE src/grid_stages.cpp)
target_sources(niagara_bridge_native PRIVATE src/grid_scatter.cpp)

target_sources(niagara_bridge_native PRIVATE src/document_copy.cpp)

target_sources(niagara_bridge_native PRIVATE src/texture_builder.cpp src/texture_payload.cpp src/texture_evidence.cpp)

target_sources(niagara_bridge_native PRIVATE src/t3d_parser.cpp src/material_graph_loader.cpp)

target_sources(niagara_bridge_native PRIVATE src/material_values.cpp src/custom_arithmetic.cpp src/material_nodes.cpp src/material_math.cpp src/material_scene_nodes.cpp src/material_support.cpp src/material_textures.cpp src/material_compile.cpp src/material_custom_shader.cpp)
target_sources(niagara_bridge_native PRIVATE src/vm_volume_controls.cpp)

target_sources(niagara_bridge_native PRIVATE src/material_programs.cpp src/material_authoring.cpp src/raster_material_program.cpp src/material_composer.cpp src/material_derived.cpp)

target_sources(niagara_bridge_native PRIVATE src/scene_context.cpp src/editor_models.cpp)

target_sources(niagara_bridge_native PRIVATE src/editor_clipboard.cpp src/editor_timeline_preview.cpp)

target_sources(niagara_bridge_native PRIVATE src/editor_actions.cpp)

target_sources(niagara_bridge_native PRIVATE src/document_values.cpp src/workspace_document.cpp src/workspace_session.cpp)
target_sources(niagara_bridge_native PRIVATE src/particle_view.cpp)
target_sources(niagara_bridge_native PRIVATE src/renderer_rows.cpp)
target_sources(niagara_bridge_native PRIVATE src/renderer_multimesh.cpp)
