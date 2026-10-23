cmake_minimum_required(VERSION 3.24)
project(TriAevumTevProgramTests LANGUAGES CXX)
enable_testing()
add_executable(frame_time_distribution_tests frame_time_distribution_tests.cpp)
target_compile_features(frame_time_distribution_tests PRIVATE cxx_std_20)
target_include_directories(frame_time_distribution_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../runtime/three_ds_recomp/include")
add_test(NAME frame_time_distribution COMMAND frame_time_distribution_tests)
add_executable(presentation_pacer_tests presentation_pacer_tests.cpp
    "${CMAKE_CURRENT_LIST_DIR}/../../oot3d/native_game_runtime/oot3d_native_presentation_pacer.cpp")
target_compile_features(presentation_pacer_tests PRIVATE cxx_std_20)
target_include_directories(presentation_pacer_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../oot3d/native_game_runtime")
add_test(NAME presentation_pacer COMMAND presentation_pacer_tests)
find_package(Vulkan REQUIRED)
find_path(SHADERC_INCLUDE_DIR shaderc/shaderc.hpp HINTS "$ENV{VULKAN_SDK}" PATH_SUFFIXES include Include REQUIRED)
find_library(SHADERC_LIBRARY NAMES shaderc_shared shaderc HINTS "$ENV{VULKAN_SDK}" PATH_SUFFIXES lib Lib REQUIRED)
get_filename_component(runtime "${CMAKE_CURRENT_LIST_DIR}/../../../runtime/three_ds_recomp" ABSOLUTE)
add_executable(texture_source_cache_tests texture_source_cache_tests.cpp
    "${runtime}/src/fast/oot3d/grass_texture_source_cache.cpp"
    "${runtime}/src/fast/oot3d/grass_texture_twins.cpp"
    "${runtime}/src/fast/oot3d/texture_preview_artifact.cpp"
    "${runtime}/src/fast/oot3d/grass_surface_extractor.cpp"
    "${runtime}/src/fast/oot3d/grass_overlay_mask.cpp"
    "${runtime}/src/fast/oot3d/grass_visibility.cpp")
target_compile_features(texture_source_cache_tests PRIVATE cxx_std_20)
target_include_directories(texture_source_cache_tests PRIVATE "${runtime}/include")
add_test(NAME texture_source_cache COMMAND texture_source_cache_tests)
add_executable(grass_spacing_tests grass_spacing_tests.cpp
    "${runtime}/src/fast/oot3d/grass_surface_extractor.cpp"
    "${runtime}/src/fast/oot3d/grass_overlay_mask.cpp"
    "${runtime}/src/fast/oot3d/grass_visibility.cpp")
target_compile_features(grass_spacing_tests PRIVATE cxx_std_20)
target_include_directories(grass_spacing_tests PRIVATE "${runtime}/include")
add_test(NAME grass_spacing COMMAND grass_spacing_tests)
add_executable(grass_cluster_order_tests grass_cluster_order_tests.cpp)
target_compile_features(grass_cluster_order_tests PRIVATE cxx_std_20)
target_include_directories(grass_cluster_order_tests PRIVATE "${runtime}/include")
add_test(NAME grass_cluster_order COMMAND grass_cluster_order_tests)
add_executable(grass_partition_tests grass_partition_tests.cpp
    "${runtime}/src/fast/oot3d/grass_visibility.cpp")
target_compile_features(grass_partition_tests PRIVATE cxx_std_20)
target_include_directories(grass_partition_tests PRIVATE "${runtime}/include")
add_test(NAME grass_partition COMMAND grass_partition_tests)
add_library(pica_tev_program STATIC "${runtime}/src/fast/renderer3ds/pica_tev_program.cpp")
target_include_directories(pica_tev_program PUBLIC "${runtime}/include")
target_compile_features(pica_tev_program PUBLIC cxx_std_20)
add_executable(pica_tev_gpu_tests tev_gpu_tests.cpp)
add_library(canonical_fragment_compile_check OBJECT
    "${CMAKE_CURRENT_LIST_DIR}/../../oot3d/native_pica_frontend/oot3d_native_pica_fragment_shader_gen.cpp")
target_compile_features(canonical_fragment_compile_check PRIVATE cxx_std_20)
target_include_directories(canonical_fragment_compile_check PRIVATE "${runtime}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../../triaevum_release/vendor/include")
target_include_directories(pica_tev_gpu_tests PRIVATE "${SHADERC_INCLUDE_DIR}")
target_link_libraries(pica_tev_gpu_tests PRIVATE pica_tev_program Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    find_file(SHADERC_RUNTIME shaderc_shared.dll HINTS "$ENV{VULKAN_SDK}" PATH_SUFFIXES Bin bin REQUIRED)
    add_custom_command(TARGET pica_tev_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:pica_tev_gpu_tests>")
endif()
add_test(NAME pica_tev_gpu COMMAND pica_tev_gpu_tests)
get_filename_component(frontend "${CMAKE_CURRENT_LIST_DIR}/../../oot3d/native_pica_frontend" ABSOLUTE)
get_filename_component(game_runtime "${CMAKE_CURRENT_LIST_DIR}/../../oot3d/native_game_runtime" ABSOLUTE)
add_executable(tev_generator_consumer_tests generator_consumer_tests.cpp
    "${frontend}/oot3d_native_pica_fragment_shader_gen.cpp"
    "${frontend}/oot3d_native_pica_fragment_lighting_gen.cpp"
    "${frontend}/oot3d_native_pica_proctex.cpp"
    "${runtime}/src/fast/oot3d/pica_fragment_lighting.cpp")
target_include_directories(tev_generator_consumer_tests PRIVATE "${frontend}" "${runtime}/include"
    "${SHADERC_INCLUDE_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../../triaevum_release/vendor/include")
target_link_libraries(tev_generator_consumer_tests PRIVATE pica_tev_program "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET tev_generator_consumer_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:tev_generator_consumer_tests>")
endif()
add_library(tev_consumer_compile_check OBJECT
    "${game_runtime}/oot3d_native_pica_vulkan_plan.cpp"
    "${game_runtime}/oot3d_native_pica_vulkan_bridge.cpp"
    "${game_runtime}/oot3d_native_pica_visual_savestate.cpp")
target_compile_features(tev_consumer_compile_check PRIVATE cxx_std_20)
target_include_directories(tev_consumer_compile_check PRIVATE "${frontend}" "${runtime}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../../triaevum_release/vendor/include")
add_test(NAME tev_generator_consumer COMMAND tev_generator_consumer_tests)
set_tests_properties(tev_generator_consumer PROPERTIES TIMEOUT 60)
add_executable(tev_uniform_codec_tests uniform_codec_tests.cpp)
target_compile_features(tev_uniform_codec_tests PRIVATE cxx_std_20)
target_include_directories(tev_uniform_codec_tests PRIVATE "${frontend}" "${game_runtime}" "${runtime}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../../triaevum_release/vendor/include")
add_test(NAME tev_uniform_codec COMMAND tev_uniform_codec_tests)
set_tests_properties(pica_tev_gpu PROPERTIES TIMEOUT 180)
add_executable(pica_lighting_gpu_tests lighting_gpu_tests.cpp
    "${frontend}/oot3d_native_pica_fragment_lighting_gen.cpp"
    "${runtime}/src/fast/oot3d/pica_fragment_lighting.cpp")
target_compile_features(pica_lighting_gpu_tests PRIVATE cxx_std_20)
target_include_directories(pica_lighting_gpu_tests PRIVATE "${frontend}" "${runtime}/include"
    "${SHADERC_INCLUDE_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../../triaevum_release/vendor/include")
target_link_libraries(pica_lighting_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET pica_lighting_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:pica_lighting_gpu_tests>")
endif()
add_test(NAME pica_lighting_gpu COMMAND pica_lighting_gpu_tests)
set_tests_properties(pica_lighting_gpu PROPERTIES TIMEOUT 180)
add_executable(surface_normal_gpu_tests surface_normal_gpu_tests.cpp
    "${runtime}/src/fast/renderer3ds/pica_vertex_surface.cpp")
target_compile_features(surface_normal_gpu_tests PRIVATE cxx_std_20)
target_include_directories(surface_normal_gpu_tests PRIVATE "${runtime}/include" "${SHADERC_INCLUDE_DIR}")
target_link_libraries(surface_normal_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET surface_normal_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:surface_normal_gpu_tests>")
endif()
add_test(NAME surface_normal_gpu COMMAND surface_normal_gpu_tests)
set_tests_properties(surface_normal_gpu PROPERTIES TIMEOUT 60)
add_executable(scene_point_light_gpu_tests scene_point_light_gpu_tests.cpp
    "${runtime}/src/fast/renderer3ds/pica_surface_lighting.cpp"
    "${runtime}/src/fast/renderer3ds/point_shadow_map.cpp")
target_compile_features(scene_point_light_gpu_tests PRIVATE cxx_std_20)
target_include_directories(scene_point_light_gpu_tests PRIVATE "${runtime}/include" "${SHADERC_INCLUDE_DIR}")
target_link_libraries(scene_point_light_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET scene_point_light_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:scene_point_light_gpu_tests>")
endif()
add_test(NAME scene_point_light_gpu COMMAND scene_point_light_gpu_tests)
set_tests_properties(scene_point_light_gpu PROPERTIES TIMEOUT 60)
add_executable(gtao_gpu_tests gtao_gpu_tests.cpp "${runtime}/src/fast/renderer/gtao_shader.cpp")
target_compile_features(gtao_gpu_tests PRIVATE cxx_std_20)
target_include_directories(gtao_gpu_tests PRIVATE "${runtime}/include" "${SHADERC_INCLUDE_DIR}")
target_link_libraries(gtao_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET gtao_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:gtao_gpu_tests>")
endif()
add_test(NAME gtao_gpu COMMAND gtao_gpu_tests)
set_tests_properties(gtao_gpu PROPERTIES TIMEOUT 60)
add_executable(diffuse_gi_gpu_tests diffuse_gi_gpu_tests.cpp
    "${runtime}/src/fast/renderer/diffuse_gi_reference.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_gather.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_scene.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_gather_shader.cpp")
target_compile_features(diffuse_gi_gpu_tests PRIVATE cxx_std_20)
target_include_directories(diffuse_gi_gpu_tests PRIVATE "${runtime}/include" "${SHADERC_INCLUDE_DIR}")
target_link_libraries(diffuse_gi_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET diffuse_gi_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:diffuse_gi_gpu_tests>")
endif()
add_test(NAME diffuse_gi_gpu COMMAND diffuse_gi_gpu_tests)
set_tests_properties(diffuse_gi_gpu PROPERTIES TIMEOUT 120)
add_executable(diffuse_gi_temporal_gpu_tests diffuse_gi_temporal_gpu_tests.cpp
    "${runtime}/src/fast/renderer/diffuse_gi_reference.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_gather.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_scene.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_gather_shader.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_temporal.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_temporal_shader.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_sequence.cpp")
target_compile_features(diffuse_gi_temporal_gpu_tests PRIVATE cxx_std_20)
target_include_directories(diffuse_gi_temporal_gpu_tests PRIVATE "${runtime}/include" "${SHADERC_INCLUDE_DIR}")
target_link_libraries(diffuse_gi_temporal_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET diffuse_gi_temporal_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:diffuse_gi_temporal_gpu_tests>")
endif()
add_test(NAME diffuse_gi_temporal_gpu COMMAND diffuse_gi_temporal_gpu_tests)
set_tests_properties(diffuse_gi_temporal_gpu PROPERTIES TIMEOUT 300)
add_executable(diffuse_gi_m4_gpu_tests diffuse_gi_m4_gpu_tests.cpp
    "${runtime}/src/fast/renderer/diffuse_gi_reference.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_gather.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_scene.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_gather_shader.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_hierarchy.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_guiding.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_noise.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_m4_shader.cpp")
target_compile_features(diffuse_gi_m4_gpu_tests PRIVATE cxx_std_20)
target_include_directories(diffuse_gi_m4_gpu_tests PRIVATE "${runtime}/include" "${SHADERC_INCLUDE_DIR}")
target_link_libraries(diffuse_gi_m4_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET diffuse_gi_m4_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:diffuse_gi_m4_gpu_tests>")
endif()
add_test(NAME diffuse_gi_m4_gpu COMMAND diffuse_gi_m4_gpu_tests)
set_tests_properties(diffuse_gi_m4_gpu PROPERTIES TIMEOUT 300)
add_executable(scene_coverage_gpu_tests scene_coverage_gpu_tests.cpp
    "${runtime}/src/fast/oot3d/pica_nri_pipeline_state.cpp")
target_compile_features(scene_coverage_gpu_tests PRIVATE cxx_std_20)
target_compile_definitions(scene_coverage_gpu_tests PRIVATE ENABLE_RENDERER3DS_VULKAN)
target_include_directories(scene_coverage_gpu_tests PRIVATE "${runtime}/include" "${SHADERC_INCLUDE_DIR}")
target_link_libraries(scene_coverage_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET scene_coverage_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:scene_coverage_gpu_tests>")
endif()
add_test(NAME scene_coverage_gpu COMMAND scene_coverage_gpu_tests)
set_tests_properties(scene_coverage_gpu PROPERTIES TIMEOUT 60)
add_executable(pica_texture_copy_derive_gpu_tests texture_copy_derive_gpu_tests.cpp)
target_compile_features(pica_texture_copy_derive_gpu_tests PRIVATE cxx_std_20)
target_include_directories(pica_texture_copy_derive_gpu_tests PRIVATE
    "${runtime}/include" "${SHADERC_INCLUDE_DIR}")
target_link_libraries(pica_texture_copy_derive_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET pica_texture_copy_derive_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:pica_texture_copy_derive_gpu_tests>")
endif()
add_test(NAME pica_texture_copy_derive_gpu COMMAND pica_texture_copy_derive_gpu_tests)
set_tests_properties(pica_texture_copy_derive_gpu PROPERTIES TIMEOUT 60)
add_executable(pica_proctex_gpu_tests proctex_gpu_tests.cpp "${frontend}/oot3d_native_pica_proctex.cpp")
target_compile_features(pica_proctex_gpu_tests PRIVATE cxx_std_20)
target_include_directories(pica_proctex_gpu_tests PRIVATE "${frontend}" "${runtime}/include"
    "${SHADERC_INCLUDE_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../../triaevum_release/vendor/include")
target_link_libraries(pica_proctex_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET pica_proctex_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:pica_proctex_gpu_tests>")
endif()
add_test(NAME pica_proctex_gpu COMMAND pica_proctex_gpu_tests)
set_tests_properties(pica_proctex_gpu PROPERTIES TIMEOUT 180)
add_executable(pica_pipeline_identity_tests pipeline_identity_tests.cpp)
target_compile_features(pica_pipeline_identity_tests PRIVATE cxx_std_20)
target_compile_definitions(pica_pipeline_identity_tests PRIVATE ENABLE_RENDERER3DS_VULKAN)
target_include_directories(pica_pipeline_identity_tests PRIVATE "${runtime}/include")
target_link_libraries(pica_pipeline_identity_tests PRIVATE Vulkan::Vulkan)
add_test(NAME pica_pipeline_identity COMMAND pica_pipeline_identity_tests)
add_executable(pica_pipeline_library_tests pipeline_library_tests.cpp)
target_compile_features(pica_pipeline_library_tests PRIVATE cxx_std_20)
target_include_directories(pica_pipeline_library_tests PRIVATE
    "${runtime}/cmake/patches/nri_pipeline_libraries" "${runtime}/include")
target_link_libraries(pica_pipeline_library_tests PRIVATE Vulkan::Vulkan)
add_test(NAME pica_pipeline_libraries COMMAND pica_pipeline_library_tests)
add_executable(pica_preparation_queue_tests preparation_queue_tests.cpp
    "${runtime}/src/fast/renderer3ds/pica_preparation_queue.cpp")
target_compile_features(pica_preparation_queue_tests PRIVATE cxx_std_20)
target_include_directories(pica_preparation_queue_tests PRIVATE "${runtime}/include")
add_test(NAME pica_preparation_queue COMMAND pica_preparation_queue_tests)

add_executable(build_fragment_artifacts build_fragment_artifacts.cpp
    "${frontend}/oot3d_native_pica_fragment_shader_gen.cpp"
    "${frontend}/oot3d_native_pica_fragment_lighting_gen.cpp"
    "${frontend}/oot3d_native_pica_proctex.cpp"
    "${runtime}/src/fast/oot3d/pica_fragment_lighting.cpp"
    "${runtime}/src/fast/renderer3ds/pica_nri_shader_contract.cpp")
target_compile_features(build_fragment_artifacts PRIVATE cxx_std_20)
target_include_directories(build_fragment_artifacts PRIVATE "${frontend}" "${runtime}/include"
    "${SHADERC_INCLUDE_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../../triaevum_release/vendor/include")
target_link_libraries(build_fragment_artifacts PRIVATE pica_tev_program "${SHADERC_LIBRARY}")
target_sources(build_fragment_artifacts PRIVATE
    "${runtime}/src/fast/renderer3ds/pica_pbr_seam_shader.cpp"
    "${runtime}/src/fast/renderer3ds/pica_pbr_shading.cpp"
    "${runtime}/src/fast/renderer3ds/pica_surface_lighting.cpp"
    "${runtime}/src/fast/renderer3ds/point_shadow_map.cpp"
    "${runtime}/src/fast/renderer3ds/pica_vertex_surface.cpp"
    "${runtime}/src/fast/renderer3ds/virtual_shadow_map.cpp"
    "${runtime}/src/fast/oot3d/directional_shadows.cpp"
    "${runtime}/src/fast/oot3d/pica_directional_shadow_caster.cpp"
    "${runtime}/src/fast/oot3d/pica_uber_effect_program.cpp"
    "${runtime}/src/fast/oot3d/pica_directional_shadow_lighting.cpp"
    "${runtime}/src/fast/oot3d/pica_directional_shadow_semantics.cpp"
    "${runtime}/src/fast/oot3d/pica_reactive_mask.cpp"
    "${runtime}/src/fast/oot3d/pica_reflection_material.cpp"
    "${runtime}/src/fast/oot3d/pica_toon_shader.cpp"
    "${runtime}/src/fast/oot3d/pica_shader_pipeline_cache.cpp"
    "${runtime}/src/fast/oot3d/pica_rigid_motion.cpp"
    "${runtime}/src/fast/oot3d/pica_shader_instrumentation.cpp")
include("${runtime}/cmake/spirv_pack.cmake")
foreach(_pack renderer3ds/pica_native_fragment oot3d/pica_temporal_fragment oot3d/pica_toon_fragment
              oot3d/pica_uber_fragment oot3d/pica_caster_fragment)
    triaevum_add_spirv_pack(build_fragment_artifacts "${runtime}/src/fast/${_pack}_binaries.cpp"
        "${runtime}/src/fast/${_pack}.spvpack")
endforeach()
add_test(NAME pica_fragment_artifacts COMMAND build_fragment_artifacts --check)
add_test(NAME pica_temporal_fragment_artifacts COMMAND build_fragment_artifacts --temporal --check)
add_test(NAME pica_toon_fragment_artifacts COMMAND build_fragment_artifacts --toon --check)
add_test(NAME pica_uber_fragment_artifacts COMMAND build_fragment_artifacts --uber --check)
set_tests_properties(pica_toon_fragment_artifacts PROPERTIES TIMEOUT 120)
set_tests_properties(pica_temporal_fragment_artifacts PROPERTIES TIMEOUT 180)
set_tests_properties(pica_fragment_artifacts PROPERTIES TIMEOUT 120)
if(WIN32)
    add_custom_command(TARGET build_fragment_artifacts POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:build_fragment_artifacts>")
endif()

find_package(spdlog CONFIG REQUIRED)
include("${runtime}/cmake/pass_shader_sources.cmake")
add_executable(build_pass_artifacts build_pass_artifacts.cpp)
triaevum_add_pass_shader_sources(build_pass_artifacts)
target_compile_features(build_pass_artifacts PRIVATE cxx_std_20)
target_include_directories(build_pass_artifacts PRIVATE "${runtime}/include" "${SHADERC_INCLUDE_DIR}")
target_link_libraries(build_pass_artifacts PRIVATE "${SHADERC_LIBRARY}" ${CMAKE_DL_LIBS})
if(WIN32)
    add_custom_command(TARGET build_pass_artifacts POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:build_pass_artifacts>")
endif()
add_test(NAME builtin_pass_artifacts COMMAND build_pass_artifacts --check)
set_tests_properties(builtin_pass_artifacts PROPERTIES TIMEOUT 120)

# The iMMERSE module's GPU tests and pass artifacts live with the module, under
# its own licence (runtime/three_ds_recomp/src/fast/immerse/LICENSE.md).
option(THREE_DS_RECOMP_ENABLE_IMMERSE "Build the iMMERSE module's tests (MXAO), under its own licence" ON)
if(THREE_DS_RECOMP_ENABLE_IMMERSE)
    include("${runtime}/src/fast/immerse/tests/gpu_tests.cmake")
endif()

add_executable(grass_motion_gpu_tests grass_motion_gpu_tests.cpp
    "${runtime}/src/fast/oot3d/grass_gpu_instance_compactor.cpp"
    "${runtime}/src/fast/oot3d/grass_interaction_field.cpp")
triaevum_add_pass_shader_sources(grass_motion_gpu_tests)
target_compile_features(grass_motion_gpu_tests PRIVATE cxx_std_20)
target_compile_definitions(grass_motion_gpu_tests PRIVATE ENABLE_OOT3D_VULKAN)
target_include_directories(grass_motion_gpu_tests PRIVATE "${runtime}/include" "${SHADERC_INCLUDE_DIR}")
target_link_libraries(grass_motion_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}" ${CMAKE_DL_LIBS})
if(WIN32)
    add_custom_command(TARGET grass_motion_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:grass_motion_gpu_tests>")
endif()
add_test(NAME grass_motion_gpu COMMAND grass_motion_gpu_tests)
set_tests_properties(grass_motion_gpu PROPERTIES TIMEOUT 120)

# Diffuse GI runtime pass (M5 Stage A): the pass object runs the precompiled
# kernels of builtin_pass.spvpack against the FP32 twins
# (docs/TRIAEVUM_SCREEN_SPACE_GI.md, "M5"). The pass shader sources bring the
# pack, its catalogue and the GI module code the kernels' parameters use.
add_executable(diffuse_gi_pass_gpu_tests diffuse_gi_pass_gpu_tests.cpp
    "${runtime}/src/fast/renderer/diffuse_gi_scene.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_sequence.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_temporal.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_pass_plan.cpp"
    "${runtime}/src/fast/renderer/diffuse_gi_pass.cpp"
    "${runtime}/src/fast/oot3d/diffuse_gi_adapter.cpp")
# M5 Stage B: the OOT3D adapter kernel with its Vulkan recording.
set_source_files_properties("${runtime}/src/fast/oot3d/diffuse_gi_adapter.cpp" PROPERTIES
    COMPILE_DEFINITIONS ENABLE_OOT3D_VULKAN)
triaevum_add_pass_shader_sources(diffuse_gi_pass_gpu_tests)
target_compile_features(diffuse_gi_pass_gpu_tests PRIVATE cxx_std_20)
target_include_directories(diffuse_gi_pass_gpu_tests PRIVATE "${runtime}/include" "${SHADERC_INCLUDE_DIR}")
target_link_libraries(diffuse_gi_pass_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}" ${CMAKE_DL_LIBS})
if(WIN32)
    add_custom_command(TARGET diffuse_gi_pass_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:diffuse_gi_pass_gpu_tests>")
endif()
add_test(NAME diffuse_gi_pass_gpu COMMAND diffuse_gi_pass_gpu_tests)
set_tests_properties(diffuse_gi_pass_gpu PROPERTIES TIMEOUT 300)

add_executable(reflection_weight_gpu_tests reflection_weight_gpu_tests.cpp
    "${runtime}/src/fast/oot3d/hiz_reflection.cpp"
    "${runtime}/src/fast/oot3d/reflection_ibl.cpp")
target_compile_features(reflection_weight_gpu_tests PRIVATE cxx_std_20)
target_include_directories(reflection_weight_gpu_tests PRIVATE "${runtime}/include" "${SHADERC_INCLUDE_DIR}")
target_link_libraries(reflection_weight_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET reflection_weight_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:reflection_weight_gpu_tests>")
endif()
add_test(NAME reflection_weight_gpu_tests COMMAND reflection_weight_gpu_tests)
set_tests_properties(reflection_weight_gpu_tests PROPERTIES TIMEOUT 120)

add_executable(pbr_seam_gpu_tests pbr_seam_gpu_tests.cpp
    "${runtime}/src/fast/renderer3ds/pica_pbr_seams.cpp"
    "${runtime}/src/fast/renderer3ds/pica_pbr_seam_shader.cpp"
    "${runtime}/src/fast/renderer3ds/pica_pbr_shading.cpp"
    "${runtime}/src/fast/renderer3ds/pica_surface_lighting.cpp"
    "${runtime}/src/fast/renderer3ds/point_shadow_map.cpp")
target_compile_features(pbr_seam_gpu_tests PRIVATE cxx_std_20)
target_include_directories(pbr_seam_gpu_tests PRIVATE "${runtime}/include" "${SHADERC_INCLUDE_DIR}")
target_link_libraries(pbr_seam_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET pbr_seam_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:pbr_seam_gpu_tests>")
endif()
add_test(NAME pbr_seam_gpu_tests COMMAND pbr_seam_gpu_tests)
set_tests_properties(pbr_seam_gpu_tests PROPERTIES TIMEOUT 120)
add_executable(build_vertex_artifacts build_vertex_artifacts.cpp
    "${runtime}/src/fast/oot3d/pica_rigid_motion.cpp"
    "${frontend}/oot3d_native_pica_shader_gen.cpp"
    "${frontend}/../third_party/azahar_pica/video_core/shader/generator/glsl_shader_decompiler.cpp"
    "${runtime}/src/fast/renderer3ds/pica_vertex_surface.cpp"
    "${runtime}/src/fast/oot3d/pica_directional_shadow_caster.cpp"
    "${runtime}/src/fast/renderer3ds/pica_nri_shader_contract.cpp"
    "${runtime}/src/fast/renderer3ds/pica_surface_lighting.cpp")
triaevum_add_spirv_pack(build_vertex_artifacts "${frontend}/oot3d_native_vertex_binaries.cpp"
    "${frontend}/oot3d_native_vertex.spvpack")
target_compile_features(build_vertex_artifacts PRIVATE cxx_std_20)
target_include_directories(build_vertex_artifacts PRIVATE "${frontend}" "${runtime}/include"
    "${frontend}/../third_party/azahar_pica" "${SHADERC_INCLUDE_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../../triaevum_release/vendor/include")
target_link_libraries(build_vertex_artifacts PRIVATE spdlog::spdlog "${SHADERC_LIBRARY}")
set(OOT3D_SHADER_TEST_ROMFS "" CACHE PATH "Private extracted ROMFS for offline vertex artifact validation")
if(OOT3D_SHADER_TEST_ROMFS)
    add_test(NAME pica_vertex_artifacts COMMAND build_vertex_artifacts --check
        "${OOT3D_SHADER_TEST_ROMFS}/CmbVShader.shbin" "${OOT3D_SHADER_TEST_ROMFS}/profile.shbin")
    set_tests_properties(pica_vertex_artifacts PROPERTIES TIMEOUT 120)
endif()
if(MSVC)
    set_property(TARGET build_vertex_artifacts PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(WIN32)
    add_custom_command(TARGET build_vertex_artifacts POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:build_vertex_artifacts>")
endif()
add_executable(pica_vertex_program_tests vertex_program_tests.cpp)
add_executable(pica_ui_canvas_tests ui_canvas_tests.cpp)
target_compile_features(pica_ui_canvas_tests PRIVATE cxx_std_20)
target_include_directories(pica_ui_canvas_tests PRIVATE "${runtime}/include")
add_test(NAME pica_ui_canvas COMMAND pica_ui_canvas_tests)
target_compile_features(pica_vertex_program_tests PRIVATE cxx_std_20)
target_include_directories(pica_vertex_program_tests PRIVATE "${runtime}/include")
add_test(NAME pica_vertex_program COMMAND pica_vertex_program_tests)
add_executable(export_vertex_programs export_vertex_programs.cpp
    "${frontend}/../third_party/azahar_pica/video_core/shader/generator/glsl_shader_decompiler.cpp")
target_compile_features(export_vertex_programs PRIVATE cxx_std_20)
target_include_directories(export_vertex_programs PRIVATE "${runtime}/include"
    "${frontend}/../third_party/azahar_pica"
    "${CMAKE_CURRENT_LIST_DIR}/../../triaevum_release/vendor/include")
target_link_libraries(export_vertex_programs PRIVATE spdlog::spdlog)
if(MSVC)
    set_property(TARGET export_vertex_programs PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
