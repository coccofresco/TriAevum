cmake_minimum_required(VERSION 3.24)
project(TriAevumTevProgramTests LANGUAGES CXX)
enable_testing()
add_executable(frame_time_distribution_tests frame_time_distribution_tests.cpp)
target_compile_features(frame_time_distribution_tests PRIVATE cxx_std_20)
target_include_directories(frame_time_distribution_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../runtime/three_ds_recomp/include")
add_test(NAME frame_time_distribution COMMAND frame_time_distribution_tests)
find_package(Vulkan REQUIRED)
find_path(SHADERC_INCLUDE_DIR shaderc/shaderc.hpp HINTS "$ENV{VULKAN_SDK}" PATH_SUFFIXES include Include REQUIRED)
find_library(SHADERC_LIBRARY NAMES shaderc_shared shaderc HINTS "$ENV{VULKAN_SDK}" PATH_SUFFIXES lib Lib REQUIRED)
get_filename_component(runtime "${CMAKE_CURRENT_LIST_DIR}/../../../runtime/three_ds_recomp" ABSOLUTE)
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
    "${runtime}/src/fast/oot3d/directional_shadows.cpp"
    "${runtime}/src/fast/oot3d/pica_directional_shadow_caster.cpp"
    "${runtime}/src/fast/oot3d/pica_directional_shadow_lighting.cpp"
    "${runtime}/src/fast/oot3d/pica_directional_shadow_semantics.cpp"
    "${runtime}/src/fast/oot3d/pica_reactive_mask.cpp"
    "${runtime}/src/fast/oot3d/pica_reflection_material.cpp"
    "${runtime}/src/fast/oot3d/pica_toon_shader.cpp"
    "${runtime}/src/fast/oot3d/pica_shader_instrumentation.cpp")
add_test(NAME pica_fragment_artifacts COMMAND build_fragment_artifacts --check)
add_test(NAME pica_temporal_fragment_artifacts COMMAND build_fragment_artifacts --temporal --check)
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
add_executable(build_vertex_artifacts build_vertex_artifacts.cpp
    "${runtime}/src/fast/oot3d/pica_rigid_motion.cpp"
    "${frontend}/oot3d_native_pica_shader_gen.cpp"
    "${frontend}/../third_party/azahar_pica/video_core/shader/generator/glsl_shader_decompiler.cpp")
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
