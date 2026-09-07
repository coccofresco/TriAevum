#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 5 || $# -gt 6 ]]; then
    printf 'usage: %s GENERATED_DIR AOT_PROGRAM SELECTION CODE_BIN EXHEADER [BUILD_DIR]\n' "$0" >&2
    exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)
generated_dir=$1
aot_program=$2
selection=$3
code_bin=$4
exheader=$5
build_dir=${6:-${repo_root}/build-linux-product}
dependency_root=${repo_root}/build-linux-deps
vulkan_include=${dependency_root}/vulkan-headers/usr/include
nlohmann_cmake=${dependency_root}/nlohmann-json/usr/share/cmake/nlohmann_json
tinyxml_cmake=${dependency_root}/tinyxml2/usr/lib/cmake/tinyxml2

for required in \
    "${generated_dir}/oot3d_whole_aot_generated.cpp" \
    "${generated_dir}/whole_aot_cpp_manifest.json" \
    "${aot_program}" "${selection}" "${code_bin}" "${exheader}" \
    "${vulkan_include}/vulkan/vulkan.h" \
    "${nlohmann_cmake}/nlohmann_jsonConfig.cmake" \
    "${tinyxml_cmake}/tinyxml2-config.cmake"; do
    if [[ ! -e ${required} ]]; then
        printf 'required Linux product input is missing: %s\n' "${required}" >&2
        exit 2
    fi
done

cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DOOT3D_ENABLE_VULKAN_RENDERER=ON \
    -DTHREE_DS_RECOMP_ENABLE_VULKAN=ON \
    -DTHREE_DS_RECOMP_ENABLE_NRI=OFF \
    -DTHREE_DS_RECOMP_ENABLE_CACAO=OFF \
    -DTHREE_DS_RECOMP_ENABLE_SSSR=OFF \
    -DOOT3D_WHOLE_AOT_PRODUCT_MODE=ON \
    -DOOT3D_REQUIRE_WHOLE_AOT=ON \
    -DOOT3D_REBUILD_WHOLE_AOT=ON \
    -DOOT3D_REGENERATE_WHOLE_AOT=OFF \
    -DOOT3D_WHOLE_AOT_GENERATED_DIR="${generated_dir}" \
    -DOOT3D_WHOLE_AOT_PROGRAM="${aot_program}" \
    -DOOT3D_WHOLE_AOT_SELECTION="${selection}" \
    -DOOT3D_WHOLE_AOT_CODE_BIN="${code_bin}" \
    -DOOT3D_WHOLE_AOT_EXHEADER="${exheader}" \
    -DOOT3D_WHOLE_AOT_MAX_PARALLEL_COMPILES=4 \
    -DVulkan_INCLUDE_DIR="${vulkan_include}" \
    -Dnlohmann_json_DIR="${nlohmann_cmake}" \
    -Dtinyxml2_DIR="${tinyxml_cmake}"

cmake --build "${build_dir}" --target oot3d_native_game --parallel 6

printf 'Linux Vulkan whole-AOT product: %s\n' \
    "${build_dir}/oot3d_native_game"
