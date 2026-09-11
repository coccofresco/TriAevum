#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 ]]; then
    printf 'Usage: bash %s RUNTIME_BUILD [DEPENDENCIES]\n' "$0" >&2
    exit 2
fi
build="$(realpath "$1")"
deps="$(realpath "${2:-$build/_deps}")"
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
compiler="${CXX:-clang++}"
imgui="$deps/imgui-src"
objects="$build/f1-settings-smoke-objects"
mkdir -p "$objects"
imgui_objects=()
for unit in imgui imgui_draw imgui_tables imgui_widgets; do
    object="$objects/$unit.o"
    if [[ ! -f "$object" || "$imgui/$unit.cpp" -nt "$object" ||
          "$imgui/imgui.h" -nt "$object" || "$imgui/imgui_internal.h" -nt "$object" ||
          "${BASH_SOURCE[0]}" -nt "$object" ]]; then
        "$compiler" -std=c++20 -O1 -DIMGUI_ENABLE_TEST_ENGINE -I"$imgui" \
            -c "$imgui/$unit.cpp" -o "$object"
    fi
    imgui_objects+=("$object")
done
panels=()
includes=()
if [[ -n "${TRIAEVUM_NLOHMANN_INCLUDE:-}" ]]; then
    includes+=(-I"$TRIAEVUM_NLOHMANN_INCLUDE")
fi
for unit in oot3d_native_controls_settings_panel oot3d_top_screen_settings_panel \
            oot3d_game_language oot3d_game_language_panel; do
    panels+=("$build/CMakeFiles/oot3d_native_game.dir/tools/oot3d/native_game_runtime/$unit.cpp.o")
done
"$compiler" -std=c++20 -O1 -DIMGUI_ENABLE_TEST_ENGINE \
    -I"$imgui" -I"$root/runtime/three_ds_recomp/include" \
    -I"$root/tools/oot3d/native_game_runtime" -I"$root/tools/oot3d/ui_topscreen" \
    -I"$root/runtime/three_ds_recomp/src/fast/renderer" -I"$root/tools/three_ds/input" \
    "${includes[@]}" "$root/tools/triaevum_release/tests/f1_settings_panel_smoke.cpp" \
    "${panels[@]}" "${imgui_objects[@]}" -Wl,--start-group \
    "$build/three_ds_recomp_runtime/src/three_ds_recomp_runtime.a" \
    "$build/three_ds_recomp_runtime/src/fast/librenderer_extension_core.a" \
    "$build/three_ds_recomp_runtime/libstb.a" \
    "$build/liboot3d_ui_topscreen.a" "$build/liboot3d_native_control_config.a" \
    "$build/libthree_ds_recomp_input.a" -Wl,--end-group \
    -lzstd -lz -lspdlog -lfmt -pthread -ldl -o "$build/f1-settings-smoke"
"$build/f1-settings-smoke"
