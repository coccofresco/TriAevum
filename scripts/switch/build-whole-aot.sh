#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)
build_dir=${OOT3D_SWITCH_AOT_BUILD_DIR:-"${repo_root}/build-switch-whole-aot"}

generated_dir=${1:-${OOT3D_WHOLE_AOT_GENERATED_DIR:-}}
program=${2:-${OOT3D_WHOLE_AOT_PROGRAM:-}}
selection=${3:-${OOT3D_WHOLE_AOT_SELECTION:-}}
code_bin=${4:-${OOT3D_CODE_BIN:-}}
exheader=${5:-${OOT3D_EXHEADER:-}}
nlohmann_include=${6:-${OOT3D_NLOHMANN_INCLUDE_DIR:-}}

if [[ -z ${generated_dir} || -z ${program} || -z ${selection} || \
      -z ${code_bin} || -z ${exheader} || -z ${nlohmann_include} ]]; then
    printf 'usage: %s GENERATED_DIR AOT_PROGRAM SELECTION CODE_BIN EXHEADER NLOHMANN_INCLUDE_DIR\n' "$0" >&2
    printf 'The same values may be supplied through the corresponding OOT3D_* variables.\n' >&2
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
toolchain=${DEVKITPRO}/cmake/Switch.cmake

cmake -S "${repo_root}/ports/whole_aot" -B "${build_dir}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${toolchain}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOOT3D_WHOLE_AOT_GENERATED_DIR="${generated_dir}" \
    -DOOT3D_WHOLE_AOT_PROGRAM="${program}" \
    -DOOT3D_WHOLE_AOT_SELECTION="${selection}" \
    -DOOT3D_CODE_BIN="${code_bin}" \
    -DOOT3D_EXHEADER="${exheader}" \
    -DOOT3D_NLOHMANN_INCLUDE_DIR="${nlohmann_include}"

cmake --build "${build_dir}" \
    --target oot3d_native_whole_aot_portable \
    -j "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"

archive=${build_dir}/liboot3d_native_whole_aot_portable.a
if [[ ! -s ${archive} ]]; then
    printf 'Whole-AOT archive was not produced: %s\n' "${archive}" >&2
    exit 1
fi

printf 'Switch whole-AOT archive: %s\n' "${archive}"
file "${archive}"
"${DEVKITA64}/bin/aarch64-none-elf-ar" t "${archive}" | wc -l
