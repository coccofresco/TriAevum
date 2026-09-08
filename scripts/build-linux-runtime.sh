#!/usr/bin/env bash
set -euo pipefail

# Developer build only. Forge installations must consume precompiled modules.
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build="${TRIAEVUM_BUILD_DIR:-$root/../triaevum-linux-build}"
jobs="${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
args=(
  -S "$root" -B "$build" -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DOOT3D_WHOLE_AOT_PRODUCT_MODE=ON -DOOT3D_DIRECT_AOT_PLUGIN=ON
  -DOOT3D_ENABLE_VULKAN_RENDERER=ON
  # The current SSSR build invokes the Windows-only FidelityFX_SC.exe.
  -DTHREE_DS_RECOMP_ENABLE_SSSR=OFF
)
if [[ -n "${TRIAEVUM_DEPS_PREFIX:-}" ]]; then
  args+=("-DCMAKE_PREFIX_PATH=$TRIAEVUM_DEPS_PREFIX")
fi
if [[ -n "${TRIAEVUM_VULKAN_INCLUDE:-}" ]]; then
  args+=("-DVulkan_INCLUDE_DIR=$TRIAEVUM_VULKAN_INCLUDE")
fi
if [[ -n "${TRIAEVUM_UI_EVIDENCE:-}" ]]; then
  args+=("-DOOT3D_NATIVE_UI_EVIDENCE_ROOT=$TRIAEVUM_UI_EVIDENCE")
fi
cmake "${args[@]}"
cmake --build "$build" --target triaevum_public_runtime oot3d_game_module --parallel "$jobs"
