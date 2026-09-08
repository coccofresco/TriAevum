cmake_minimum_required(VERSION 3.24)
project(triaevum_controller_tests LANGUAGES CXX)

# An isolated input test: no renderer, ROM, title module or game rebuild.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_package(SDL2 2.24 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
get_filename_component(runtime_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_executable(triaevum_controller_tests
    controller_tests.cpp
    "${runtime_root}/src/ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.cpp")
target_include_directories(triaevum_controller_tests PRIVATE "${runtime_root}/include")
target_compile_definitions(triaevum_controller_tests PRIVATE SDL_MAIN_HANDLED)
if(MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 22)
    # SDL2 2.32's older Clang workaround otherwise redeclares a new builtin.
    target_compile_options(triaevum_controller_tests PRIVATE /FIintrin.h)
endif()
target_link_libraries(triaevum_controller_tests PRIVATE SDL2::SDL2 spdlog::spdlog)
enable_testing()
add_test(NAME controller_lifecycle COMMAND triaevum_controller_tests
    "${runtime_root}/../triaevum_resources/gamecontrollerdb.txt")
