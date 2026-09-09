cmake_minimum_required(VERSION 3.21)
project(TriAevumNativeProbe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(nlohmann_json CONFIG REQUIRED)
get_filename_component(TRIAEVUM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/TitleSupport.cmake")
triaevum_add_title_support("${TRIAEVUM_ROOT}" "${TRIAEVUM_ROOT}/tools/oot3d/native_a32_runtime/upstream")

# Exercise the real loader without configuring SDL, graphics SDKs, UI or a title.
set(host "${TRIAEVUM_ROOT}/tools/oot3d/native_game_runtime")
set(pinned "${TRIAEVUM_ROOT}/tools/oot3d/native_a32_runtime/upstream/recomp")
add_executable(oot3d_native_direct_aot_tests EXCLUDE_FROM_ALL
    "${host}/oot3d_native_direct_aot_tests.cpp"
    "${host}/oot3d_native_direct_aot.cpp"
    "${pinned}/a32_core.cpp"
    "${pinned}/a32_core_alu.cpp"
    "${pinned}/a32_core_memory.cpp"
    "${pinned}/a32_runtime.cpp")
target_compile_definitions(oot3d_native_direct_aot_tests PRIVATE
    OOT3D_NATIVE_DIRECT_AOT_PLUGIN=1 OOT3D_NATIVE_GENERATED_WHOLE_AOT=1 NOMINMAX)
target_link_libraries(oot3d_native_direct_aot_tests PRIVATE
    triaevum_title_whole_aot_support ${CMAKE_DL_LIBS})
if(WIN32)
    set_property(TARGET oot3d_native_direct_aot_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(triaevum_probe_empty_title SHARED EXCLUDE_FROM_ALL
    "${host}/triaevum_title_aot_stub.cpp")
target_link_libraries(triaevum_probe_empty_title PRIVATE triaevum_title_whole_aot_support)
set_target_properties(triaevum_probe_empty_title PROPERTIES PREFIX "")
if(WIN32)
    target_compile_definitions(triaevum_probe_empty_title PRIVATE NOMINMAX)
    set_property(TARGET triaevum_probe_empty_title PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
