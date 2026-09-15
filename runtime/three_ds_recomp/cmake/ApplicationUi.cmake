include(FetchContent)
set(RMLUI_FONT_ENGINE none CACHE STRING "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(RMLUI_COMPILER_OPTIONS OFF CACHE BOOL "" FORCE)
set(RMLUI_THIRDPARTY_CONTAINERS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
set(TRIAEVUM_RMLUI_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/triaevum-rmlui-build" CACHE PATH "RmlUi build directory")
FetchContent_Declare(triaevum_rmlui
    BINARY_DIR "${TRIAEVUM_RMLUI_BINARY_DIR}"
    URL https://github.com/mikke89/RmlUi/archive/refs/tags/6.1.zip
    URL_HASH SHA256=4e3561190cf7e6867388d5c89e15604f4f7bd83e112316ac0832a6a2d061eace)
FetchContent_MakeAvailable(triaevum_rmlui)
add_library(triaevum_application_ui STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/fast/appui/SettingsFrontend.cpp)
target_compile_features(triaevum_application_ui PUBLIC cxx_std_20)
target_include_directories(triaevum_application_ui PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../include)
target_link_libraries(triaevum_application_ui PRIVATE RmlUi::Core ImGui SDL2::SDL2)
target_link_libraries(three_ds_recomp_runtime PUBLIC triaevum_application_ui)
