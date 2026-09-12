cmake_minimum_required(VERSION 3.21)
project(TriAevumMacChecks LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(host "${root}/tools/oot3d/native_game_runtime")
set(pinned "${root}/tools/oot3d/native_a32_runtime/upstream")
set(fast "${root}/runtime/three_ds_recomp")

# Reuse the runtime's existing tests without development-only title sources.
add_executable(macos_pacing_tests
    pacing_main.cpp
    "${host}/oot3d_native_frame_rate_tests.cpp"
    "${host}/oot3d_native_frame_rate.cpp"
    "${host}/oot3d_native_presentation_pacer.cpp"
    "${host}/oot3d_native_a32_memory_bus_defaults.cpp"
    "${root}/tools/oot3d/typed_gameplay/oot3d_gameplay_time.cpp")
target_include_directories(macos_pacing_tests PRIVATE
    "${host}" "${pinned}" "${pinned}/recomp"
    "${root}/tools/oot3d/typed_gameplay")
add_test(NAME macos_pacing_tests COMMAND macos_pacing_tests)

include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(googletest)
find_package(Threads REQUIRED)
add_executable(macos_grass_tests
    "${fast}/tests/oot3d_grass_async_placement_builder_tests.cpp"
    "${fast}/src/fast/oot3d/grass_async_placement_builder.cpp"
    "${fast}/src/fast/oot3d/grass_static_placement_cache.cpp"
    "${fast}/src/fast/oot3d/grass_placement_cache.cpp"
    "${fast}/src/fast/oot3d/grass_surface_extractor.cpp"
    "${fast}/src/fast/oot3d/grass_world_placement_cache.cpp"
    "${fast}/src/fast/oot3d/grass_texture_source_cache.cpp"
    "${fast}/src/fast/oot3d/texture_preview_artifact.cpp"
    "${fast}/src/fast/oot3d/grass_visibility.cpp")
target_include_directories(macos_grass_tests PRIVATE "${fast}/include")
target_link_libraries(macos_grass_tests PRIVATE GTest::gtest_main Threads::Threads)
include(GoogleTest)
gtest_discover_tests(macos_grass_tests)

add_executable(macos_framebuffer_readback_tests
    "${fast}/tests/framebuffer_readback_tests.cpp")
target_include_directories(macos_framebuffer_readback_tests PRIVATE "${fast}/include")
add_test(NAME macos_framebuffer_readback_tests COMMAND macos_framebuffer_readback_tests)
