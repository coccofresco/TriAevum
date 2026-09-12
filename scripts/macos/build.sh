#!/usr/bin/env bash
set -euo pipefail

if [[ $(uname -s) != Darwin ]]; then
    printf 'This script builds natively on macOS.\n' >&2
    exit 2
fi
mode=${1:-probe}
if [[ $# -gt 2 || ( ${mode} != probe && ${mode} != runtime ) ]]; then
    printf 'usage: %s [probe|runtime] [BUILD_DIR]\n' "$0" >&2
    exit 2
fi
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)
build_dir=${2:-${repo_root}/build-macos}

for tool in cmake ninja; do
    if ! command -v "${tool}" >/dev/null; then
        printf 'Missing %s. See docs/OOT3D_MACOS_PORT.md for dependencies.\n' "${tool}" >&2
        exit 2
    fi
done

# Homebrew bottles target the installed OS. This is a local development build,
# not a claim that the resulting binary runs on older macOS releases.
cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-$(sw_vers -productVersion)}" \
    -DOOT3D_ENABLE_VULKAN_RENDERER=ON \
    -DTHREE_DS_RECOMP_ENABLE_NRI=OFF \
    -DTHREE_DS_RECOMP_ENABLE_CACAO=OFF \
    -DTHREE_DS_RECOMP_ENABLE_SSSR=OFF \
    -DOOT3D_WHOLE_AOT_PRODUCT_MODE=ON \
    -DOOT3D_DIRECT_AOT_PLUGIN=ON

if [[ ${mode} == runtime ]]; then
    cmake --build "${build_dir}" --target triaevum_public_runtime \
        --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-6}"
    printf 'Runtime built. Use the macos_title CMake project and package_macos.py for the playable app.\n'
else
    cmake --build "${build_dir}" --target oot3d_shadow2d_backend_probe \
        triaevum_input_service_tests triaevum_audio_service_tests \
        --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-6}"
    (
        cd -- "${build_dir}"
        ./triaevum_module/triaevum_input_service_tests
        ./triaevum_module/triaevum_audio_service_tests
        ./oot3d_shadow2d_backend_probe --renderer vulkan \
            --resource-root "${repo_root}/runtime/three_ds_recomp/src/fast" \
            --output macos-shadow2d-probe.json \
            --screenshot macos-shadow2d-probe.bmp
    )
    printf 'macOS renderer probe passed: %s/macos-shadow2d-probe.json\n' "${build_dir}"
fi
