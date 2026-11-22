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
  # No C++ modules are used; minimal Clang SDKs omit clang-scan-deps.
  -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
  -DOOT3D_WHOLE_AOT_PRODUCT_MODE=ON -DOOT3D_DIRECT_AOT_PLUGIN=ON
  -DOOT3D_ENABLE_VULKAN_RENDERER=ON
)
if [[ -n "${TRIAEVUM_FFX_SHADER_BUNDLE:-}" ]]; then
  args+=( -DTHREE_DS_RECOMP_ENABLE_SSSR=ON
    "-DTHREE_DS_RECOMP_FFX_SHADER_BUNDLE=$TRIAEVUM_FFX_SHADER_BUNDLE" )
else
  args+=( -DTHREE_DS_RECOMP_ENABLE_SSSR=OFF )
fi
if [[ -n "${TRIAEVUM_DEPS_PREFIX:-}" ]]; then
  args+=("-DCMAKE_PREFIX_PATH=$TRIAEVUM_DEPS_PREFIX")
fi
if [[ -n "${TRIAEVUM_VULKAN_INCLUDE:-}" ]]; then
  args+=("-DVulkan_INCLUDE_DIR=$TRIAEVUM_VULKAN_INCLUDE")
fi
cmake "${args[@]}"
cmake --build "$build" --target triaevum_public_runtime oot3d_game_module --parallel "$jobs"
