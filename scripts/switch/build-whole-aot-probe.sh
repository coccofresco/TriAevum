#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)
build_dir=${OOT3D_SWITCH_AOT_BUILD_DIR:-"${repo_root}/build-switch-whole-aot"}

if [[ $# -ne 6 ]]; then
    printf 'usage: %s GENERATED_DIR AOT_PROGRAM SELECTION CODE_BIN EXHEADER NLOHMANN_INCLUDE_DIR\n' "$0" >&2
    exit 2
fi

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

cmake -S "${repo_root}/ports/whole_aot" -B "${build_dir}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOOT3D_WHOLE_AOT_GENERATED_DIR="$1" \
    -DOOT3D_WHOLE_AOT_PROGRAM="$2" \
    -DOOT3D_WHOLE_AOT_SELECTION="$3" \
    -DOOT3D_CODE_BIN="$4" \
    -DOOT3D_EXHEADER="$5" \
    -DOOT3D_NLOHMANN_INCLUDE_DIR="$6" \
    -DOOT3D_WHOLE_AOT_BUILD_SWITCH_PROBE=ON

cmake --build "${build_dir}" \
    --target oot3d_whole_aot_switch_probe_nro \
    -j "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"

nro=${build_dir}/oot3d_whole_aot_switch_probe.nro
elf=${build_dir}/oot3d_whole_aot_switch_probe.elf
if [[ ! -s ${nro} || ! -s ${elf} ]]; then
    printf 'Switch whole-AOT probe outputs are missing.\n' >&2
    exit 1
fi

header=$(xxd -p -l 4 -s 16 "${nro}")
if [[ ${header} != 4e524f30 ]]; then
    printf 'Invalid NRO0 header: %s\n' "${header}" >&2
    exit 1
fi
if ! "${DEVKITA64}/bin/aarch64-none-elf-readelf" -h "${elf}" | \
        grep -q 'Machine:.*AArch64'; then
    printf 'Probe ELF is not AArch64.\n' >&2
    exit 1
fi

printf 'Switch whole-AOT NRO: %s\n' "${nro}"
sha256sum "${nro}"
