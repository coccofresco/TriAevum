# UI and input: the UI contract, TopScreen, N64 UI, lifecycle bridge, UI textures and fonts, the input
# library, control configuration and A32 input, and their tests.
# Included from the root CMakeLists.txt, in its scope.

add_subdirectory(tools/oot3d/ui_contract EXCLUDE_FROM_ALL)

add_library(oot3d_ui_topscreen STATIC EXCLUDE_FROM_ALL
    tools/oot3d/ui_topscreen/oot3d_native_font_coverage.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_config.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_controls.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_gameplay_actions.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_dpad_presentation.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_items_hint.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_camera.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_camera_guest.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_item_guest.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_item_dispatch.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_mod_profile.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_mod_profile_items.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_mod_profile_hud.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_mod_profile_quest.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_mod_profile_pause.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_ocarina.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_texture_overrides.cpp
)
target_include_directories(oot3d_ui_topscreen PUBLIC
    tools/oot3d/native_game_runtime
    tools/oot3d/ui_topscreen
)
target_link_libraries(oot3d_ui_topscreen PUBLIC
    three_ds_recomp_input
    oot3d_native_a32_process
    oot3d_native_ui_contract)
oot3d_target_defaults(oot3d_ui_topscreen FOLDER Tools)

add_executable(oot3d_native_font_coverage_tests EXCLUDE_FROM_ALL
    tools/oot3d/ui_topscreen/oot3d_native_font_coverage.cpp
    tools/oot3d/ui_topscreen/oot3d_native_font_coverage_tests.cpp)
target_compile_features(oot3d_native_font_coverage_tests PRIVATE cxx_std_20)
set_property(TARGET oot3d_native_font_coverage_tests PROPERTY FOLDER Tests)

add_executable(oot3d_top_screen_mod_profile_tests EXCLUDE_FROM_ALL
    tools/oot3d/ui_topscreen/oot3d_top_screen_mod_profile_tests.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_item_dispatch_tests.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_ocarina_tests.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_dpad_presentation_tests.cpp
)
target_link_libraries(oot3d_top_screen_mod_profile_tests PRIVATE
    oot3d_ui_topscreen)
oot3d_target_defaults(oot3d_top_screen_mod_profile_tests FOLDER Tests)

add_executable(oot3d_top_screen_items_hint_tests EXCLUDE_FROM_ALL
    tools/oot3d/ui_topscreen/oot3d_top_screen_items_hint_tests.cpp
)
target_link_libraries(oot3d_top_screen_items_hint_tests PRIVATE
    oot3d_ui_topscreen)
target_compile_options(oot3d_top_screen_items_hint_tests PRIVATE
    "${WARNING_OVERRIDE}")
oot3d_target_defaults(oot3d_top_screen_items_hint_tests NO_WARNING_OVERRIDE FOLDER Tests)

add_executable(oot3d_native_ui_contract_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_ui_contract_tests.cpp
)
target_link_libraries(oot3d_native_ui_contract_tests PRIVATE
    oot3d_native_ui_contract)
oot3d_target_defaults(oot3d_native_ui_contract_tests FOLDER Tests)

add_library(oot3d_ui_n64 STATIC EXCLUDE_FROM_ALL
    tools/oot3d/ui_n64/oot3d_native_hud_atlas_regions.h
    tools/oot3d/ui_n64/oot3d_n64_gameplay_hud.cpp
    tools/oot3d/ui_n64/oot3d_n64_integrated_ui_runtime.cpp
)
target_include_directories(oot3d_ui_n64 PUBLIC
    tools/oot3d/ui_n64
)
target_link_libraries(oot3d_ui_n64 PUBLIC
    oot3d_native_ui_contract)
oot3d_target_defaults(oot3d_ui_n64 FOLDER Tools)

add_library(oot3d_ui_n64_renderer STATIC EXCLUDE_FROM_ALL
    tools/oot3d/ui_n64/oot3d_n64_ui_renderer.cpp
)
target_include_directories(oot3d_ui_n64_renderer PUBLIC
    tools/oot3d/ui_n64
)
target_link_libraries(oot3d_ui_n64_renderer PUBLIC
    oot3d_ui_n64
    oot3d_renderer_contracts)
oot3d_target_defaults(oot3d_ui_n64_renderer FOLDER Tools)

add_library(oot3d_ui_legacy_adapters STATIC EXCLUDE_FROM_ALL
    tools/oot3d/ui_n64/oot3d_legacy_ui_adapters.cpp
)
target_include_directories(oot3d_ui_legacy_adapters PUBLIC
    tools/oot3d/ui_n64
)
target_link_libraries(oot3d_ui_legacy_adapters PUBLIC
    oot3d_ui_n64_renderer
    oot3d_renderer_contracts
    PRIVATE ImGui)
oot3d_target_defaults(oot3d_ui_legacy_adapters FOLDER Tools)

add_executable(oot3d_n64_integrated_ui_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/ui_n64/oot3d_n64_integrated_ui_runtime_tests.cpp
)
target_link_libraries(oot3d_n64_integrated_ui_runtime_tests PRIVATE
    oot3d_ui_n64)
oot3d_target_defaults(oot3d_n64_integrated_ui_runtime_tests FOLDER Tests)

add_executable(oot3d_n64_gameplay_hud_tests EXCLUDE_FROM_ALL
    tools/oot3d/ui_n64/oot3d_n64_gameplay_hud_tests.cpp
)
target_link_libraries(oot3d_n64_gameplay_hud_tests PRIVATE
    oot3d_ui_n64)
oot3d_target_defaults(oot3d_n64_gameplay_hud_tests FOLDER Tests)

add_executable(oot3d_n64_ui_renderer_tests EXCLUDE_FROM_ALL
    tools/oot3d/ui_n64/oot3d_n64_ui_renderer_tests.cpp
)
target_link_libraries(oot3d_n64_ui_renderer_tests PRIVATE
    oot3d_ui_n64_renderer)
if (DEFINED _oot3d_mass_clang_builtins)
    target_link_libraries(oot3d_n64_ui_renderer_tests PRIVATE
        "${_oot3d_mass_clang_builtins}")
endif()
oot3d_target_defaults(oot3d_n64_ui_renderer_tests FOLDER Tests)

add_library(oot3d_top_screen_runtime_consumer STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_top_screen_gameplay_action_consumer.cpp
    tools/oot3d/native_game_runtime/oot3d_top_screen_items_hint_consumer.cpp
)
target_include_directories(oot3d_top_screen_runtime_consumer PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_top_screen_runtime_consumer PUBLIC
    oot3d_native_a32_process
    oot3d_ui_topscreen)
target_compile_options(oot3d_top_screen_runtime_consumer PRIVATE
    "${WARNING_OVERRIDE}")
oot3d_target_defaults(oot3d_top_screen_runtime_consumer NO_WARNING_OVERRIDE FOLDER Tools)

add_library(oot3d_native_ui_lifecycle_bridge STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_ui_lifecycle_bridge.cpp
)
target_include_directories(oot3d_native_ui_lifecycle_bridge PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_native_ui_lifecycle_bridge PUBLIC
    oot3d_native_a32_process
    oot3d_ui_n64
    oot3d_ui_topscreen
    oot3d_top_screen_runtime_consumer)
oot3d_target_defaults(oot3d_native_ui_lifecycle_bridge FOLDER Tools)

add_executable(oot3d_native_ui_lifecycle_bridge_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_ui_lifecycle_bridge_tests.cpp
)
target_link_libraries(oot3d_native_ui_lifecycle_bridge_tests PRIVATE
    oot3d_native_ui_lifecycle_bridge)
oot3d_target_defaults(oot3d_native_ui_lifecycle_bridge_tests FOLDER Tests)

add_library(oot3d_native_ui_texture_provider STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_ui_texture_provider.cpp
)
target_include_directories(oot3d_native_ui_texture_provider PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_native_ui_texture_provider PUBLIC
    oot3d_native_pica_submission
    oot3d_ui_n64_renderer
    oot3d_ui_topscreen
    oot3d_renderer_texture_decode)
oot3d_target_defaults(oot3d_native_ui_texture_provider FOLDER Tools)

add_executable(oot3d_native_ui_texture_provider_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_ui_texture_provider_tests.cpp
)
target_link_libraries(oot3d_native_ui_texture_provider_tests PRIVATE
    oot3d_native_ui_texture_provider)
oot3d_target_defaults(oot3d_native_ui_texture_provider_tests FOLDER Tests)

add_executable(oot3d_top_screen_ocarina_text_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_top_screen_ocarina_text_runtime.cpp
    tools/oot3d/native_game_runtime/oot3d_top_screen_ocarina_text_runtime_tests.cpp)
target_link_libraries(oot3d_top_screen_ocarina_text_runtime_tests PRIVATE
    oot3d_native_ui_texture_provider oot3d_native_a32_process)
target_compile_options(oot3d_top_screen_ocarina_text_runtime_tests PRIVATE "${WARNING_OVERRIDE}")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_top_screen_ocarina_text_runtime_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_top_screen_ocarina_text_runtime_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_top_screen_ocarina_text_runtime_tests PROPERTY FOLDER Tests)

add_executable(oot3d_top_screen_pause_transaction_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_top_screen_pause_transaction.cpp
    tools/oot3d/native_game_runtime/oot3d_top_screen_pause_transaction_tests.cpp)
target_link_libraries(oot3d_top_screen_pause_transaction_tests PRIVATE
    oot3d_ui_topscreen oot3d_native_a32_process)
target_compile_options(oot3d_top_screen_pause_transaction_tests PRIVATE "${WARNING_OVERRIDE}")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_top_screen_pause_transaction_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_top_screen_pause_transaction_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_top_screen_pause_transaction_tests PROPERTY FOLDER Tests)

add_executable(oot3d_top_screen_item_description_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_top_screen_item_description_runtime.cpp
    tools/oot3d/native_game_runtime/oot3d_top_screen_item_description_runtime_tests.cpp)
target_link_libraries(oot3d_top_screen_item_description_runtime_tests PRIVATE
    oot3d_ui_topscreen oot3d_native_a32_process)
target_compile_options(oot3d_top_screen_item_description_runtime_tests PRIVATE "${WARNING_OVERRIDE}")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_top_screen_item_description_runtime_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_top_screen_item_description_runtime_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_top_screen_item_description_runtime_tests PROPERTY FOLDER Tests)

add_executable(oot3d_top_screen_wide_items_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_top_screen_wide_items_runtime.cpp
    tools/oot3d/native_game_runtime/oot3d_top_screen_wide_items_runtime_tests.cpp)
target_link_libraries(oot3d_top_screen_wide_items_runtime_tests PRIVATE
    oot3d_ui_topscreen oot3d_native_a32_process)
target_compile_options(oot3d_top_screen_wide_items_runtime_tests PRIVATE "${WARNING_OVERRIDE}")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_top_screen_wide_items_runtime_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_top_screen_wide_items_runtime_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_top_screen_wide_items_runtime_tests PROPERTY FOLDER Tests)

find_package(libzip REQUIRED)
add_library(oot3d_top_screen_texture_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_top_screen_texture_runtime.cpp
    tools/oot3d/native_game_runtime/oot3d_native_hd_font_runtime.cpp
)
target_include_directories(oot3d_top_screen_texture_runtime PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_top_screen_texture_runtime PUBLIC
    libzip::zip
    oot3d_native_ui_lifecycle_bridge
    oot3d_native_ui_texture_provider)
target_compile_options(oot3d_top_screen_texture_runtime PRIVATE "${WARNING_OVERRIDE}")
add_executable(oot3d_native_hd_font_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_hd_font_runtime_tests.cpp)
target_link_libraries(oot3d_native_hd_font_runtime_tests PRIVATE oot3d_top_screen_texture_runtime)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_property(TARGET oot3d_native_hd_font_runtime_tests PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
oot3d_target_defaults(oot3d_top_screen_texture_runtime NO_WARNING_OVERRIDE FOLDER Tools)

add_executable(oot3d_top_screen_texture_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_top_screen_texture_runtime_tests.cpp
)
target_link_libraries(oot3d_top_screen_texture_runtime_tests PRIVATE
    oot3d_top_screen_texture_runtime)
oot3d_target_defaults(oot3d_top_screen_texture_runtime_tests FOLDER Tests)

add_library(three_ds_recomp_input STATIC EXCLUDE_FROM_ALL
    tools/three_ds/input/three_ds_input.cpp
    tools/three_ds/input/three_ds_motion_composition.cpp
)
target_include_directories(three_ds_recomp_input PUBLIC
    tools/three_ds/input
)
oot3d_target_defaults(three_ds_recomp_input FOLDER Tools)

add_executable(three_ds_recomp_input_tests EXCLUDE_FROM_ALL
    tools/three_ds/input/three_ds_input_tests.cpp
)
target_link_libraries(three_ds_recomp_input_tests PRIVATE
    three_ds_recomp_input)
target_compile_options(three_ds_recomp_input_tests PRIVATE
    "${WARNING_OVERRIDE}")
oot3d_target_defaults(three_ds_recomp_input_tests NO_WARNING_OVERRIDE FOLDER Tests)

# Imported dependencies found in the renderer directory are not visible here.
if (NOT TARGET SDL2::SDL2 AND CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$")
    find_package(SDL2 REQUIRED)
endif()
if (TARGET SDL2::SDL2)
    add_executable(three_ds_sdl_controller_tests EXCLUDE_FROM_ALL
        tools/three_ds/input/three_ds_sdl_controller_tests.cpp)
    target_link_libraries(three_ds_sdl_controller_tests PRIVATE three_ds_recomp_input SDL2::SDL2)
    oot3d_target_defaults(three_ds_sdl_controller_tests NO_WARNING_OVERRIDE FOLDER Tests)
endif()

add_library(oot3d_native_control_config STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_control_config.cpp
)
target_include_directories(oot3d_native_control_config PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_native_control_config PUBLIC
    nlohmann_json::nlohmann_json
    three_ds_recomp_input)
target_compile_options(oot3d_native_control_config PRIVATE
    "${WARNING_OVERRIDE}")
oot3d_target_defaults(oot3d_native_control_config NO_WARNING_OVERRIDE FOLDER Tools)

add_library(oot3d_native_a32_input STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_input.cpp
)
target_include_directories(oot3d_native_a32_input PUBLIC
    tools/oot3d/native_game_runtime
)
target_include_directories(oot3d_native_a32_input PRIVATE
    runtime/three_ds_recomp/include
)
target_link_libraries(oot3d_native_a32_input PUBLIC
    nlohmann_json::nlohmann_json
    oot3d_native_control_config)
oot3d_target_defaults(oot3d_native_a32_input FOLDER Tools)

# The UI lifecycle bridge consumes the guest-input routing contract. Declare
# the dependency after both targets exist so the shared 3DS input API and its
# include directory propagate deterministically.
target_link_libraries(oot3d_native_ui_lifecycle_bridge PUBLIC
    oot3d_native_a32_input)

add_executable(oot3d_native_a32_input_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_input_tests.cpp
)
target_link_libraries(oot3d_native_a32_input_tests PRIVATE
    oot3d_native_a32_input)
oot3d_target_defaults(oot3d_native_a32_input_tests FOLDER Tests)
