cmake_minimum_required(VERSION 3.24)
project(TriAevumTevProgramTests LANGUAGES CXX)
enable_testing()
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
