#!/usr/bin/env bash
# Configure and build the public TriAevum runtime (plugin mode) on Linux.
#
# This is the Linux counterpart of the Windows `triaevum-direct-module-build`
# cache consumed by tools/triaevum_release/prepare_release.py. It needs no
# ROM, generated C++ or title inputs: the runtime loads
# triaevum_title_aot.so at run time and ships with the empty stub.
#
# System packages: clang, lld, cmake >= 3.30, SDL2, Vulkan loader + headers,
# shaderc, nlohmann-json, spdlog, libpng, libzip, opus/opusfile/ogg/vorbis.
# tinyxml2 must provide a CMake config; scripts/linux/stage-deps.sh builds one
# under build-linux-deps when the distribution package lacks it.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)
build_dir=${1:-${repo_root}/build-linux-public-runtime}
dependency_root=${repo_root}/build-linux-deps
extra=()
for candidate in \
    "tinyxml2_DIR=${dependency_root}/tinyxml2/usr/lib/cmake/tinyxml2" \
    "nlohmann_json_DIR=${dependency_root}/nlohmann-json/usr/share/cmake/nlohmann_json" \
    "Vulkan_INCLUDE_DIR=${dependency_root}/vulkan-headers/usr/include"; do
    if [[ -e ${candidate#*=} ]]; then
        extra+=("-D${candidate}")
    fi
done

# NRI is required: Forge's product contract rejects a runtime without it.
# FidelityFX SSSR/FSR and NGX/DLSS stay off; their SDK adapters are
# Windows-only in this tree. CACAO needs a DXC on PATH and is opt-in.
cmake -S "${repo_root}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="${CC:-clang}" \
    -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
    -DCMAKE_LINKER_TYPE=LLD \
    -DOOT3D_ENABLE_VULKAN_RENDERER=ON \
    -DTHREE_DS_RECOMP_ENABLE_NRI=ON \
    -DTHREE_DS_RECOMP_ENABLE_CACAO="${TRIAEVUM_ENABLE_CACAO:-OFF}" \
    -DTHREE_DS_RECOMP_ENABLE_SSSR=OFF \
    -DOOT3D_WHOLE_AOT_PRODUCT_MODE=ON \
    -DOOT3D_DIRECT_AOT_PLUGIN=ON \
    "${extra[@]}"

cmake --build "${build_dir}" \
    --target triaevum_public_runtime oot3d_game_module triaevum_title_whole_aot_support \
    --parallel "${TRIAEVUM_BUILD_JOBS:-$(nproc)}"

printf 'Linux public runtime: %s\n' "${build_dir}/TriAevum"
printf 'Title plugin stub:    %s\n' "${build_dir}/triaevum_title_aot.so"
printf 'Runtime targets:      %s\n' "${build_dir}/triaevum-runtime-targets-Release.json"
