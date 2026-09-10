include(FetchContent)

add_executable(framebuffer_readback_tests framebuffer_readback_tests.cpp)
target_compile_features(framebuffer_readback_tests PRIVATE cxx_std_20)
target_include_directories(framebuffer_readback_tests PRIVATE ../include)
add_test(NAME framebuffer_readback_tests COMMAND framebuffer_readback_tests)

add_executable(native_presentation_policy_tests native_presentation_policy_tests.cpp)
target_compile_features(native_presentation_policy_tests PRIVATE cxx_std_20)
target_include_directories(native_presentation_policy_tests PRIVATE ../include)
add_test(NAME native_presentation_policy_tests COMMAND native_presentation_policy_tests)

add_executable(vulkan_present_dispatch_tests
    vulkan_present_dispatch_tests.cpp
    ../src/fast/oot3d/vulkan_adapter_policy.cpp)
target_compile_features(vulkan_present_dispatch_tests PRIVATE cxx_std_20)
target_include_directories(vulkan_present_dispatch_tests PRIVATE ../include)
add_test(NAME vulkan_present_dispatch_tests COMMAND vulkan_present_dispatch_tests)

if(TARGET SDL2::SDL2)
    add_executable(sdl_video_driver_policy_tests sdl_video_driver_policy_tests.cpp)
    target_compile_features(sdl_video_driver_policy_tests PRIVATE cxx_std_20)
    target_include_directories(sdl_video_driver_policy_tests PRIVATE ../include)
    target_link_libraries(sdl_video_driver_policy_tests PRIVATE SDL2::SDL2)
    add_test(NAME sdl_video_driver_policy_tests COMMAND sdl_video_driver_policy_tests)
    set_tests_properties(sdl_video_driver_policy_tests PROPERTIES
        ENVIRONMENT_MODIFICATION "SDL_VIDEODRIVER=unset:")
endif()

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.16.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(gtest PRIVATE -Wno-character-conversion)
    target_compile_options(gtest_main PRIVATE -Wno-character-conversion)
    if(MSVC)
        target_compile_options(gtest PRIVATE /WX-)
        target_compile_options(gtest_main PRIVATE /WX-)
    endif()
endif()

add_executable(three_ds_recomp_tests
    binary_io_tests.cpp
    string_helper_tests.cpp
    crc64_tests.cpp
    stox_tests.cpp
    utils_tests.cpp
    bitconverter_tests.cpp
    event_system_tests.cpp
    sound_matrix_decoder_tests.cpp
    path_file_helper_tests.cpp
    archive_resource_tests.cpp
    glob_tests.cpp
    splittext_tests.cpp
    path_diskfile_tests.cpp
    resource_type_tests.cpp
    oot3d_native_resource_contract_tests.cpp
    oot3d_asset_catalog_tests.cpp
    oot3d_semantic_route_catalog_tests.cpp
    oot3d_native_qdb_provider_tests.cpp
    oot3d_native_scene_entry_provider_tests.cpp
    oot3d_cutscene_provider_tests.cpp
    archive_self_tests.cpp
)

if(ENABLE_SCRIPTING)
    target_sources(three_ds_recomp_tests PRIVATE script_test.cpp)
    if(WIN32)
        add_custom_command(TARGET three_ds_recomp_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:libtcc>
                $<TARGET_FILE_DIR:three_ds_recomp_tests>
        )
    endif()
endif()

set_property(TARGET three_ds_recomp_tests PROPERTY CXX_STANDARD 20)

target_link_libraries(three_ds_recomp_tests PRIVATE
    three_ds_recomp_runtime
    gtest_main
)

target_compile_definitions(three_ds_recomp_tests PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_OFF)
if(MSVC)
    target_compile_options(three_ds_recomp_tests PRIVATE /bigobj)
    if(THREE_DS_RECOMP_FAST_DEV_LINK)
        target_link_options(three_ds_recomp_tests PRIVATE /DEBUG:NONE /OPT:REF /OPT:NOICF)
    endif()
endif()

include(GoogleTest)
gtest_discover_tests(three_ds_recomp_tests DISCOVERY_TIMEOUT 30)

add_executable(mouse_capture_policy_tests mouse_capture_policy_tests.cpp)
add_test(NAME mouse_capture_policy_tests COMMAND mouse_capture_policy_tests)
if(TARGET NRI)
    add_executable(nri_stage_scope_tests nri_stage_scope_tests.cpp)
    target_compile_definitions(nri_stage_scope_tests PRIVATE ENABLE_OOT3D_NRI)
    target_include_directories(nri_stage_scope_tests PRIVATE ../include)
    target_link_libraries(nri_stage_scope_tests PRIVATE NRI)
    add_test(NAME nri_stage_scope_tests COMMAND nri_stage_scope_tests)
endif()

add_executable(oot3d_vulkan_diagnostics_tests
    oot3d_vulkan_diagnostics_tests.cpp
    ../src/fast/backends/oot3d_vulkan_diagnostics.cpp
    ../src/fast/oot3d/effect_geometry_provider_plan.cpp
    ../src/fast/oot3d/effect_graph.cpp
)
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json REQUIRED)
endif()
if(NOT TARGET spdlog::spdlog)
    find_package(spdlog REQUIRED)
endif()
if(NOT TARGET ZLIB::ZLIB)
    find_package(ZLIB REQUIRED)
endif()
set_property(TARGET oot3d_vulkan_diagnostics_tests PROPERTY CXX_STANDARD 20)
target_include_directories(oot3d_vulkan_diagnostics_tests PRIVATE
    ../include
)
target_link_libraries(oot3d_vulkan_diagnostics_tests PRIVATE
    gtest_main
    nlohmann_json::nlohmann_json
    ThreeDsRecomp::RendererExtensionCore
    ThreeDsRecomp::Renderer3dsPicaCore
)
if(MSVC)
    target_compile_definitions(oot3d_vulkan_diagnostics_tests PRIVATE NOMINMAX)
    target_compile_options(oot3d_vulkan_diagnostics_tests PRIVATE /utf-8)
endif()
gtest_discover_tests(oot3d_vulkan_diagnostics_tests DISCOVERY_TIMEOUT 30)

add_executable(oot3d_pica_scene_frame_tests
    oot3d_pica_scene_frame_tests.cpp
    ../src/fast/oot3d/directional_shadows.cpp
    ../src/fast/oot3d/native_scene_view.cpp
    ../src/fast/oot3d/pica_directional_shadow_semantics.cpp
    ../src/fast/oot3d/pica_scene_frame.cpp
    ../src/fast/oot3d/pica_scene_publication_adapter.cpp
    ../src/fast/oot3d/pica_scene_semantics.cpp
)
set_property(TARGET oot3d_pica_scene_frame_tests PROPERTY CXX_STANDARD 20)
target_include_directories(oot3d_pica_scene_frame_tests PRIVATE ../include)
target_link_libraries(oot3d_pica_scene_frame_tests PRIVATE
    gtest_main
    ThreeDsRecomp::RendererExtensionCore
    ThreeDsRecomp::Renderer3dsPicaCore)
if(MSVC)
    target_compile_definitions(oot3d_pica_scene_frame_tests PRIVATE NOMINMAX)
    target_compile_options(oot3d_pica_scene_frame_tests PRIVATE /utf-8)
endif()
gtest_discover_tests(oot3d_pica_scene_frame_tests DISCOVERY_TIMEOUT 30)

add_executable(oot3d_pica_shader_pipeline_cache_tests
    oot3d_pica_shader_pipeline_cache_tests.cpp
    ../src/fast/oot3d/directional_shadows.cpp
    ../src/fast/oot3d/pica_ambient_occlusion_guide.cpp
    ../src/fast/oot3d/pica_directional_shadow_caster.cpp
    ../src/fast/oot3d/pica_directional_shadow_lighting.cpp
    ../src/fast/oot3d/pica_directional_shadow_semantics.cpp
    ../src/fast/oot3d/pica_reactive_mask.cpp
    ../src/fast/oot3d/pica_reflection_material.cpp
    ../src/fast/oot3d/pica_rigid_motion.cpp
    ../src/fast/oot3d/pica_scene_domain_guide.cpp
    ../src/fast/oot3d/pica_shader_instrumentation.cpp
    ../src/fast/oot3d/pica_shader_pipeline_cache.cpp
    ../src/fast/oot3d/pica_toon_shader.cpp
)
set_property(TARGET oot3d_pica_shader_pipeline_cache_tests
    PROPERTY CXX_STANDARD 20)
target_include_directories(oot3d_pica_shader_pipeline_cache_tests
    PRIVATE ../include)
target_link_libraries(oot3d_pica_shader_pipeline_cache_tests
    PRIVATE gtest_main)
if(TARGET three_ds_recomp_shaderc)
    target_link_libraries(oot3d_pica_shader_pipeline_cache_tests
        PRIVATE three_ds_recomp_shaderc)
    target_compile_definitions(oot3d_pica_shader_pipeline_cache_tests
        PRIVATE OOT3D_PIPELINE_CACHE_HAS_SHADERC=1)
endif()
if(MSVC)
    target_compile_definitions(oot3d_pica_shader_pipeline_cache_tests
        PRIVATE NOMINMAX)
    target_compile_options(oot3d_pica_shader_pipeline_cache_tests
        PRIVATE /utf-8)
endif()
gtest_discover_tests(oot3d_pica_shader_pipeline_cache_tests
    DISCOVERY_TIMEOUT 30)

add_executable(oot3d_pica_pipeline_manifest_tests
    oot3d_pica_pipeline_manifest_tests.cpp
    ../src/fast/oot3d/pica_aot_shader_pack.cpp
    ../src/fast/oot3d/pica_pipeline_manifest.cpp)
set_property(TARGET oot3d_pica_pipeline_manifest_tests PROPERTY CXX_STANDARD 20)
target_include_directories(oot3d_pica_pipeline_manifest_tests PRIVATE ../include)
target_link_libraries(oot3d_pica_pipeline_manifest_tests PRIVATE
    gtest_main
    nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_definitions(oot3d_pica_pipeline_manifest_tests PRIVATE NOMINMAX)
    target_compile_options(oot3d_pica_pipeline_manifest_tests PRIVATE /utf-8)
endif()
gtest_discover_tests(oot3d_pica_pipeline_manifest_tests
    DISCOVERY_TIMEOUT 30)

add_executable(oot3d_pica_grass_texture_coordinates_tests
    oot3d_pica_grass_texture_coordinates_tests.cpp
    ../src/fast/oot3d/pica_grass_texture_coordinates.cpp
)
set_property(TARGET oot3d_pica_grass_texture_coordinates_tests
    PROPERTY CXX_STANDARD 20)
target_include_directories(oot3d_pica_grass_texture_coordinates_tests
    PRIVATE ../include)
target_link_libraries(oot3d_pica_grass_texture_coordinates_tests
    PRIVATE gtest_main)
if(MSVC)
    target_compile_definitions(oot3d_pica_grass_texture_coordinates_tests
        PRIVATE NOMINMAX)
    target_compile_options(oot3d_pica_grass_texture_coordinates_tests
        PRIVATE /utf-8)
endif()
gtest_discover_tests(oot3d_pica_grass_texture_coordinates_tests
    DISCOVERY_TIMEOUT 30)

add_executable(renderer_extension_schedule_tests
    renderer_extension_schedule_tests.cpp
)
set_property(TARGET renderer_extension_schedule_tests PROPERTY CXX_STANDARD 20)
target_include_directories(renderer_extension_schedule_tests PRIVATE ../include)
target_link_libraries(renderer_extension_schedule_tests PRIVATE
    gtest_main
    ThreeDsRecomp::RendererExtensionCore)
if(MSVC)
    target_compile_definitions(renderer_extension_schedule_tests PRIVATE NOMINMAX)
    target_compile_options(renderer_extension_schedule_tests PRIVATE /utf-8)
endif()
gtest_discover_tests(renderer_extension_schedule_tests DISCOVERY_TIMEOUT 30)

add_executable(renderer_extension_resource_graph_tests
    renderer_extension_resource_graph_tests.cpp
)
set_property(TARGET renderer_extension_resource_graph_tests PROPERTY CXX_STANDARD 20)
target_include_directories(renderer_extension_resource_graph_tests PRIVATE ../include)
target_link_libraries(renderer_extension_resource_graph_tests PRIVATE
    gtest_main
    ThreeDsRecomp::RendererExtensionCore)
if(MSVC)
    target_compile_definitions(renderer_extension_resource_graph_tests PRIVATE NOMINMAX)
    target_compile_options(renderer_extension_resource_graph_tests PRIVATE /utf-8)
endif()
gtest_discover_tests(renderer_extension_resource_graph_tests
    DISCOVERY_TIMEOUT 30)

add_executable(renderer_extension_resource_allocation_tests
    renderer_extension_resource_allocation_tests.cpp
)
set_property(TARGET renderer_extension_resource_allocation_tests
    PROPERTY CXX_STANDARD 20)
target_include_directories(renderer_extension_resource_allocation_tests
    PRIVATE ../include)
target_link_libraries(renderer_extension_resource_allocation_tests
    PRIVATE
        gtest_main
        ThreeDsRecomp::RendererExtensionCore)
if(MSVC)
    target_compile_definitions(renderer_extension_resource_allocation_tests
        PRIVATE NOMINMAX)
    target_compile_options(renderer_extension_resource_allocation_tests
        PRIVATE /utf-8)
endif()
gtest_discover_tests(renderer_extension_resource_allocation_tests
    DISCOVERY_TIMEOUT 30)

add_executable(renderer_extension_resource_binding_tests
    renderer_extension_resource_binding_tests.cpp
)
set_property(TARGET renderer_extension_resource_binding_tests
    PROPERTY CXX_STANDARD 20)
target_include_directories(renderer_extension_resource_binding_tests
    PRIVATE ../include)
target_link_libraries(renderer_extension_resource_binding_tests
    PRIVATE
        gtest_main
        ThreeDsRecomp::RendererExtensionCore)
if(MSVC)
    target_compile_definitions(renderer_extension_resource_binding_tests
        PRIVATE NOMINMAX)
    target_compile_options(renderer_extension_resource_binding_tests
        PRIVATE /utf-8)
endif()
gtest_discover_tests(renderer_extension_resource_binding_tests
    DISCOVERY_TIMEOUT 30)

add_executable(renderer_extension_scene_publication_tests
    renderer_extension_scene_publication_tests.cpp
)
set_property(TARGET renderer_extension_scene_publication_tests
    PROPERTY CXX_STANDARD 20)
target_include_directories(renderer_extension_scene_publication_tests
    PRIVATE ../include)
target_link_libraries(renderer_extension_scene_publication_tests
    PRIVATE
        gtest_main
        ThreeDsRecomp::RendererExtensionCore)
if(MSVC)
    target_compile_definitions(renderer_extension_scene_publication_tests
        PRIVATE NOMINMAX)
    target_compile_options(renderer_extension_scene_publication_tests
        PRIVATE /utf-8)
endif()
gtest_discover_tests(renderer_extension_scene_publication_tests
    DISCOVERY_TIMEOUT 30)

add_executable(renderer_3ds_pica_scene_capabilities_tests
    renderer_3ds_pica_scene_capabilities_tests.cpp
    renderer_3ds_pica_gl_shader_dialect_tests.cpp
)
set_property(TARGET renderer_3ds_pica_scene_capabilities_tests
    PROPERTY CXX_STANDARD 20)
target_include_directories(renderer_3ds_pica_scene_capabilities_tests
    PRIVATE ../include)
target_link_libraries(renderer_3ds_pica_scene_capabilities_tests
    PRIVATE
        gtest_main
        ThreeDsRecomp::Renderer3dsPicaCore)
if(MSVC)
    target_compile_definitions(renderer_3ds_pica_scene_capabilities_tests
        PRIVATE NOMINMAX)
    target_compile_options(renderer_3ds_pica_scene_capabilities_tests
        PRIVATE /utf-8)
endif()
gtest_discover_tests(renderer_3ds_pica_scene_capabilities_tests
    DISCOVERY_TIMEOUT 30)

add_custom_target(verify_renderer_3ds_cross_title_boundary
    COMMAND ${CMAKE_COMMAND}
        "-DRENDERER3DS_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/.."
        "-DRENDERER3DS_CANARY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/renderer_3ds_cross_title_canary_tests.cpp"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/VerifyRenderer3dsCrossTitleBoundary.cmake"
    COMMENT "Verifying title-neutral Nintendo 3DS renderer boundary"
    VERBATIM)

add_executable(renderer_3ds_cross_title_canary_tests
    renderer_3ds_cross_title_canary_tests.cpp
)
set_property(TARGET renderer_3ds_cross_title_canary_tests
    PROPERTY CXX_STANDARD 20)
target_include_directories(renderer_3ds_cross_title_canary_tests
    PRIVATE ../include)
target_link_libraries(renderer_3ds_cross_title_canary_tests
    PRIVATE
        gtest_main
        ThreeDsRecomp::Renderer3dsPicaCore)
add_dependencies(renderer_3ds_cross_title_canary_tests
    verify_renderer_3ds_cross_title_boundary)
if(MSVC)
    target_compile_definitions(renderer_3ds_cross_title_canary_tests
        PRIVATE NOMINMAX)
    target_compile_options(renderer_3ds_cross_title_canary_tests
        PRIVATE /utf-8)
endif()
gtest_discover_tests(renderer_3ds_cross_title_canary_tests
    DISCOVERY_TIMEOUT 30)

add_library(renderer_3ds_pica_capture_conformance STATIC
    renderer_3ds_pica_capture_conformance.cpp
)
set_property(TARGET renderer_3ds_pica_capture_conformance
    PROPERTY CXX_STANDARD 20)
target_include_directories(renderer_3ds_pica_capture_conformance
    PUBLIC . ../include)
target_link_libraries(renderer_3ds_pica_capture_conformance
    PUBLIC
        nlohmann_json::nlohmann_json
        ThreeDsRecomp::Renderer3dsPicaCore)
add_dependencies(renderer_3ds_pica_capture_conformance
    verify_renderer_3ds_cross_title_boundary)
if(MSVC)
    target_compile_definitions(renderer_3ds_pica_capture_conformance
        PRIVATE NOMINMAX)
    target_compile_options(renderer_3ds_pica_capture_conformance
        PRIVATE /utf-8)
endif()

add_executable(renderer_3ds_pica_capture_conformance_tests
    renderer_3ds_pica_capture_conformance_tests.cpp
)
set_property(TARGET renderer_3ds_pica_capture_conformance_tests
    PROPERTY CXX_STANDARD 20)
target_link_libraries(renderer_3ds_pica_capture_conformance_tests
    PRIVATE
        gtest_main
        renderer_3ds_pica_capture_conformance)
if(MSVC)
    target_compile_definitions(renderer_3ds_pica_capture_conformance_tests
        PRIVATE NOMINMAX)
    target_compile_options(renderer_3ds_pica_capture_conformance_tests
        PRIVATE /utf-8)
endif()
gtest_discover_tests(renderer_3ds_pica_capture_conformance_tests
    DISCOVERY_TIMEOUT 30)

add_executable(renderer_3ds_pica_capture_inspector
    renderer_3ds_pica_capture_inspector.cpp
)
set_property(TARGET renderer_3ds_pica_capture_inspector
    PROPERTY CXX_STANDARD 20)
target_link_libraries(renderer_3ds_pica_capture_inspector
    PRIVATE renderer_3ds_pica_capture_conformance)
if(MSVC)
    target_compile_definitions(renderer_3ds_pica_capture_inspector
        PRIVATE NOMINMAX)
    target_compile_options(renderer_3ds_pica_capture_inspector
        PRIVATE /utf-8)
endif()

add_executable(oot3d_effect_graph_tests
    oot3d_effect_graph_tests.cpp
    ../src/fast/oot3d/effect_geometry_provider_plan.cpp
    ../src/fast/oot3d/effect_graph.cpp
    ../src/fast/oot3d/effect_graph_pass_barriers.cpp
    ../src/fast/oot3d/effect_graph_physical_plan.cpp
    ../src/fast/oot3d/hiz_depth_pyramid.cpp
    ../src/fast/oot3d/display_effect_plan.cpp
    ../src/fast/oot3d/display_effect_resources.cpp
    ../src/fast/oot3d/pica_extension_schedule.cpp
    ../src/fast/oot3d/pica_guide_sampling_barriers.cpp
    ../src/fast/oot3d/upscaler_contract.cpp
)
set_property(TARGET oot3d_effect_graph_tests PROPERTY CXX_STANDARD 20)
target_include_directories(oot3d_effect_graph_tests PRIVATE ../include)
target_link_libraries(oot3d_effect_graph_tests PRIVATE
    gtest_main
    Vulkan::Vulkan
    ThreeDsRecomp::RendererExtensionCore
    ThreeDsRecomp::Renderer3dsPicaCore
)
if(MSVC)
    target_compile_definitions(oot3d_effect_graph_tests PRIVATE NOMINMAX)
    target_compile_options(oot3d_effect_graph_tests PRIVATE /utf-8)
endif()
gtest_discover_tests(oot3d_effect_graph_tests DISCOVERY_TIMEOUT 30)

add_executable(oot3d_graphics_foundation_tests
    oot3d_graphics_foundation_tests.cpp
    oot3d_azahar_texture_pack_tests.cpp
    oot3d_graphics_settings_persistence_tests.cpp
    oot3d_graphics_settings_runtime_tests.cpp
    oot3d_grass_async_placement_builder_tests.cpp
    oot3d_grass_geometry_registry_tests.cpp
    oot3d_grass_world_placement_cache_tests.cpp
    oot3d_pica_display_transfer_tests.cpp
    oot3d_pica_geometry_registry_tests.cpp
    oot3d_pica_texture_decode_tests.cpp
    oot3d_texture_assignment_tests.cpp
    config_atomic_save_tests.cpp
    ../src/ship/config/ConfigPersistence.cpp
    ../src/fast/oot3d/effect_graph.cpp
    ../src/fast/oot3d/display_effect_plan.cpp
    ../src/fast/oot3d/display_effect_resources.cpp
    ../src/fast/oot3d/ambient_occlusion_composite.cpp
    ../src/fast/oot3d/azahar_texture_pack.cpp
    ../src/fast/oot3d/cacao_normal_input.cpp
    ../src/fast/oot3d/grass_async_placement_builder.cpp
    ../src/fast/oot3d/grass_blade_geometry.cpp
    ../src/fast/oot3d/grass_collision_resolver.cpp
    ../src/fast/oot3d/grass_geometry_registry.cpp
    ../src/fast/oot3d/grass_world_placement_cache.cpp
    ../src/fast/oot3d/grass_interaction_bridge.cpp
    ../src/fast/oot3d/grass_interaction_field.cpp
    ../src/fast/oot3d/grass_motion.cpp
    ../src/fast/oot3d/grass_placement_cache.cpp
    ../src/fast/oot3d/grass_primitive_topology.cpp
    ../src/fast/oot3d/grass_render_telemetry.cpp
    ../src/fast/oot3d/grass_scene_bridge.cpp
    ../src/fast/oot3d/grass_shading_environment.cpp
    ../src/fast/oot3d/pica_scene_semantics.cpp
    ../src/fast/oot3d/grass_surface_extractor.cpp
    ../src/fast/oot3d/grass_texture_source_cache.cpp
    ../src/fast/oot3d/grass_visibility.cpp
    ../src/fast/oot3d/grass_wind.cpp
    ../src/fast/oot3d/graphics_capabilities.cpp
    ../src/fast/oot3d/graphics_settings.cpp
    ../src/fast/oot3d/directional_shadows.cpp
    ../src/fast/oot3d/graphics_settings_persistence.cpp
    ../src/fast/oot3d/graphics_settings_runtime.cpp
    ../src/fast/oot3d/texture_assignment_model.cpp
    ../src/fast/oot3d/texture_catalog_selection.cpp
    ../src/fast/oot3d/texture_preview_artifact.cpp
    ../src/fast/oot3d/gpu_profile_plan.cpp
    ../src/fast/oot3d/presentation_settings_transaction.cpp
    ../src/fast/oot3d/renderer_presentation_controller.cpp
    ../src/fast/oot3d/renderer_validation_telemetry.cpp
    ../src/fast/oot3d/reflection_ibl.cpp
    ../src/fast/oot3d/reflection_debug.cpp
    ../src/fast/oot3d/reflection_material_profile.cpp
    ../src/fast/oot3d/reflection_provider.cpp
    ../src/fast/oot3d/vulkan_adapter_policy.cpp
    ../src/fast/oot3d/hiz_depth_pyramid.cpp
    ../src/fast/oot3d/hiz_reflection.cpp
    ../src/fast/oot3d/linear_scene_color.cpp
    ../src/fast/oot3d/motion_vectors.cpp
    ../src/fast/oot3d/pica_fragment_lighting.cpp
    ../src/fast/renderer3ds/pica_nri_shader_contract.cpp
    ../src/fast/oot3d/pica_nri_texture_upload.cpp
    ../src/fast/renderer3ds/pica_nri_upload.cpp
    ../src/fast/oot3d/pica_nri_vertex_input.cpp
    ../src/fast/oot3d/pica_ambient_occlusion_guide.cpp
    ../src/fast/oot3d/pica_grass_texture_coordinates.cpp
    ../src/fast/oot3d/pica_scene_domain_guide.cpp
    ../src/fast/oot3d/pica_display_transfer.cpp
    ../src/fast/oot3d/pica_geometry_registry.cpp
    ../src/fast/oot3d/pica_guide_sampling_barriers.cpp
    ../src/fast/oot3d/perspective_fov_policy.cpp
    ../src/fast/oot3d/pica_reactive_mask.cpp
    ../src/fast/oot3d/pica_reflection_material.cpp
    ../src/fast/oot3d/pica_rigid_motion.cpp
    ../src/fast/oot3d/pica_texture_decode.cpp
    ../src/fast/oot3d/pica_toon_shader.cpp
    ../src/fast/oot3d/pica_toon_telemetry.cpp
    ../src/fast/oot3d/pica_scanout_effects.cpp
    ../src/fast/oot3d/spatial_aa.cpp
    ../src/fast/oot3d/temporal_history_manager.cpp
    ../src/fast/oot3d/temporal_jitter.cpp
    ../src/fast/oot3d/temporal_aa.cpp
    ../src/fast/oot3d/upscaler_contract.cpp
    ../src/fast/oot3d/pica_shadow2d.cpp
    ../src/fast/oot3d/scene_view_bridge.cpp
    ../src/fast/oot3d/scene_surface_registry.cpp
    ../src/fast/oot3d/scene_composite.cpp
    ../src/fast/oot3d/toon_outline_shader.cpp
    ../src/fast/oot3d/resource_state_tracker.cpp
    ../src/fast/oot3d/smaa_1x.cpp
    ../src/fast/oot3d/smaa_lookup_data.cpp
    ../src/fast/oot3d/visual_clock.cpp
)
set_property(TARGET oot3d_graphics_foundation_tests PROPERTY CXX_STANDARD 20)
target_include_directories(oot3d_graphics_foundation_tests PRIVATE ../include)
target_link_libraries(oot3d_graphics_foundation_tests PRIVATE
    gtest_main
    nlohmann_json::nlohmann_json
    ThreeDsRecomp::RendererExtensionCore
    spdlog::spdlog
    stb
    ZLIB::ZLIB
)
if(TARGET three_ds_recomp_shaderc)
    target_link_libraries(oot3d_graphics_foundation_tests PRIVATE
        three_ds_recomp_shaderc)
    target_compile_definitions(oot3d_graphics_foundation_tests PRIVATE
        OOT3D_FOUNDATION_HAS_SHADERC=1)
endif()
if(MSVC)
    target_compile_definitions(oot3d_graphics_foundation_tests PRIVATE NOMINMAX)
    target_compile_options(oot3d_graphics_foundation_tests PRIVATE /utf-8)
endif()
gtest_discover_tests(oot3d_graphics_foundation_tests DISCOVERY_TIMEOUT 30)

# Small GPU contract test: no runtime/title rebuild or game assets required.
if(TARGET three_ds_recomp_shaderc AND TARGET Vulkan::Vulkan)
    add_executable(oot3d_toon_outline_gpu_tests
        oot3d_toon_outline_gpu_tests.cpp
        oot3d_grass_lod_gpu_tests.cpp
        ../src/fast/oot3d/toon_outline_shader.cpp)
    set_property(TARGET oot3d_toon_outline_gpu_tests PROPERTY CXX_STANDARD 20)
    target_include_directories(oot3d_toon_outline_gpu_tests PRIVATE ../include)
    target_link_libraries(oot3d_toon_outline_gpu_tests PRIVATE
        gtest_main three_ds_recomp_shaderc Vulkan::Vulkan)
    gtest_discover_tests(oot3d_toon_outline_gpu_tests
        DISCOVERY_TIMEOUT 30 PROPERTIES LABELS "gpu;outline")
endif()
