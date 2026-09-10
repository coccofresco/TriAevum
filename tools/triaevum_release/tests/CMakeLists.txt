# Real F1 widgets, with a private instrumented ImGui and no window or ROM.
FetchContent_GetProperties(ImGui SOURCE_DIR _f1_imgui_source)
add_executable(triaevum_f1_settings_smoke EXCLUDE_FROM_ALL
    f1_settings_panel_smoke.cpp
    ../../oot3d/native_game_runtime/oot3d_native_controls_settings_panel.cpp
    ../../oot3d/native_game_runtime/oot3d_top_screen_settings_panel.cpp
    ../../oot3d/native_game_runtime/oot3d_game_language.cpp
    ../../oot3d/native_game_runtime/oot3d_game_language_panel.cpp
    ${_f1_imgui_source}/imgui.cpp
    ${_f1_imgui_source}/imgui_draw.cpp
    ${_f1_imgui_source}/imgui_tables.cpp
    ${_f1_imgui_source}/imgui_widgets.cpp)
target_compile_features(triaevum_f1_settings_smoke PRIVATE cxx_std_20)
target_compile_definitions(triaevum_f1_settings_smoke PRIVATE IMGUI_ENABLE_TEST_ENGINE)
target_include_directories(triaevum_f1_settings_smoke PRIVATE
    ${_f1_imgui_source}
    ${PROJECT_SOURCE_DIR}/runtime/three_ds_recomp/src/fast/renderer)
target_link_libraries(triaevum_f1_settings_smoke PRIVATE
    three_ds_recomp_runtime oot3d_ui_topscreen oot3d_native_control_config)
if(WIN32)
    target_compile_definitions(triaevum_f1_settings_smoke PRIVATE NOMINMAX)
    set_property(TARGET triaevum_f1_settings_smoke PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET triaevum_f1_settings_smoke PROPERTY FOLDER Tests)
