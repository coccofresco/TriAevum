cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(triaevum_ui_contract LANGUAGES CXX)
    enable_testing()
endif()

# The checked-in manifest is the source inventory, not a private evidence path.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/SOURCE_MANIFEST.json" ui_manifest)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/SOURCE_MANIFEST.json")
string(JSON ui_file_count LENGTH "${ui_manifest}" files)
math(EXPR ui_last_file "${ui_file_count} - 1")
set(ui_sources)
foreach(index RANGE ${ui_last_file})
    string(JSON relative GET "${ui_manifest}" files ${index} path)
    if(NOT relative MATCHES "^oot3d_ui/[a-z_]+\\.(h|cpp)$")
        message(FATAL_ERROR "Invalid UI source inventory entry: ${relative}")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${relative}")
        message(FATAL_ERROR
            "Incomplete TriAevum source: missing ui_contract/${relative}. "
            "Use the complete public source package; no decompilation snapshot is required.")
    endif()
    list(APPEND ui_sources "${relative}")
endforeach()
add_library(oot3d_native_ui_contract STATIC ${ui_sources})
target_include_directories(oot3d_native_ui_contract PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(oot3d_native_ui_contract PUBLIC cxx_std_20)
set_property(TARGET oot3d_native_ui_contract PROPERTY FOLDER Tools)
if(WARNING_OVERRIDE)
    target_compile_options(oot3d_native_ui_contract PRIVATE "${WARNING_OVERRIDE}")
endif()
if(WIN32)
    target_compile_definitions(oot3d_native_ui_contract PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_ui_contract PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Exercise real consumers without a GPU, ROM, SDK or renderer dependency.
    add_executable(oot3d_native_ui_contract_tests
        ../native_game_runtime/oot3d_native_ui_contract_tests.cpp)
    add_executable(oot3d_n64_gameplay_hud_tests
        ../ui_n64/oot3d_n64_gameplay_hud_tests.cpp
        ../ui_n64/oot3d_n64_gameplay_hud.cpp)
    add_executable(oot3d_top_screen_items_hint_tests
        ../ui_topscreen/oot3d_top_screen_items_hint_tests.cpp
        ../ui_topscreen/oot3d_top_screen_items_hint.cpp)
    foreach(test IN ITEMS oot3d_native_ui_contract_tests
            oot3d_n64_gameplay_hud_tests oot3d_top_screen_items_hint_tests)
        target_link_libraries(${test} PRIVATE oot3d_native_ui_contract)
        if(WIN32)
            set_property(TARGET ${test} PROPERTY MSVC_RUNTIME_LIBRARY
                "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
        add_test(NAME ${test} COMMAND ${test})
    endforeach()
endif()
