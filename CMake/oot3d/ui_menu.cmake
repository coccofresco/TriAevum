# The in-game menu: a controller-friendly settings menu drawn by the
# integrated UI renderer (no ImGui), with its baked open fonts. The model,
# fonts and presentation are game-neutral; the rows bound to the title's
# setting owners live in native_game_runtime (oot3d_in_game_menu.cpp).
add_library(oot3d_ui_menu STATIC EXCLUDE_FROM_ALL
    tools/oot3d/ui_menu/oot3d_menu_model.cpp
    tools/oot3d/ui_menu/oot3d_menu_font.cpp
    tools/oot3d/ui_menu/oot3d_menu_font_atlas_data.cpp
    tools/oot3d/ui_menu/oot3d_menu_presentation.cpp
)
target_include_directories(oot3d_ui_menu PUBLIC tools/oot3d/ui_menu)
target_link_libraries(oot3d_ui_menu PUBLIC
    oot3d_native_ui_contract
    oot3d_ui_n64_renderer
    stb)
oot3d_target_defaults(oot3d_ui_menu FOLDER Tools)

add_executable(oot3d_menu_tests EXCLUDE_FROM_ALL
    tools/oot3d/ui_menu/oot3d_menu_tests.cpp)
target_link_libraries(oot3d_menu_tests PRIVATE oot3d_ui_menu)
oot3d_target_defaults(oot3d_menu_tests FOLDER Tests)
