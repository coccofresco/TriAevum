#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)
build_dir=${OOT3D_SWITCH_PRODUCT_BUILD_DIR:-"${repo_root}/build-switch-product"}
aot_build_dir=${OOT3D_SWITCH_AOT_BUILD_DIR:-"${repo_root}/build-switch-whole-aot"}

if [[ $# -ne 6 ]]; then
    printf 'usage: %s GENERATED_DIR AOT_PROGRAM SELECTION CODE_BIN EXHEADER NLOHMANN_INCLUDE_DIR\n' "$0" >&2
    exit 2
fi

generated_dir=$1
program=$2
selection=$3
code_bin=$4
exheader=$5
nlohmann_include=$6

for input in "${generated_dir}" "${program}" "${selection}" \
             "${code_bin}" "${exheader}" "${nlohmann_include}"; do
    if [[ ! -e ${input} ]]; then
        printf 'required product input does not exist: %s\n' "${input}" >&2
        exit 2
    fi
done

if [[ -z ${DEVKITPRO:-} ]]; then
    if [[ -f /opt/devkitpro/cmake/Switch.cmake ]]; then
        export DEVKITPRO=/opt/devkitpro
    elif [[ -f ${HOME}/.local/oot3dre-devkitpro-root/opt/devkitpro/cmake/Switch.cmake ]]; then
        export DEVKITPRO=${HOME}/.local/oot3dre-devkitpro-root/opt/devkitpro
    else
        printf 'DEVKITPRO is unset and Switch.cmake was not found.\n' >&2
        exit 2
    fi
fi

export DEVKITA64=${DEVKITA64:-"${DEVKITPRO}/devkitA64"}
export PATH="${DEVKITA64}/bin:${DEVKITPRO}/tools/bin:${PATH}"
export OOT3D_SWITCH_AOT_BUILD_DIR=${aot_build_dir}

"${script_dir}/build-whole-aot.sh" \
    "${generated_dir}" \
    "${program}" \
    "${selection}" \
    "${code_bin}" \
    "${exheader}" \
    "${nlohmann_include}"

aot_archive=${aot_build_dir}/liboot3d_native_whole_aot_portable.a
cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOOT3D_ENABLE_VULKAN_RENDERER=OFF \
    -DOOT3D_WHOLE_AOT_PRODUCT_MODE=ON \
    -DOOT3D_REQUIRE_WHOLE_AOT=ON \
    -DOOT3D_REBUILD_WHOLE_AOT=OFF \
    -DOOT3D_WHOLE_AOT_GENERATED_DIR="${generated_dir}" \
    -DOOT3D_WHOLE_AOT_PROGRAM="${program}" \
    -DOOT3D_WHOLE_AOT_SELECTION="${selection}" \
    -DOOT3D_WHOLE_AOT_CODE_BIN="${code_bin}" \
    -DOOT3D_WHOLE_AOT_EXHEADER="${exheader}" \
    -DOOT3D_WHOLE_AOT_PREBUILT_LIBRARY="${aot_archive}"

cmake --build "${build_dir}" \
    --target oot3d_native_game_nro \
    -j "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"

nro=${build_dir}/oot3d_native_game.nro
elf=${build_dir}/oot3d_native_game.elf
if [[ ! -s ${nro} || ! -s ${elf} ]]; then
    printf 'Switch product outputs are missing.\n' >&2
    exit 1
fi
if [[ $(xxd -p -l 4 -s 16 "${nro}") != 4e524f30 ]]; then
    printf 'invalid NRO0 header: %s\n' "${nro}" >&2
    exit 1
fi
if ! "${DEVKITA64}/bin/aarch64-none-elf-readelf" -h "${elf}" | \
        grep -q 'Machine:.*AArch64'; then
    printf 'product ELF is not AArch64: %s\n' "${elf}" >&2
    exit 1
fi

printf 'Switch whole-AOT product NRO: %s\n' "${nro}"
sha256sum "${nro}"
