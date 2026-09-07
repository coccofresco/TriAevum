#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)
build_dir=${OOT3D_SWITCH_VULKAN_PROBE_BUILD_DIR:-"${repo_root}/build-switch-vulkan-probe"}

if [[ $# -ne 1 ]]; then
    printf 'usage: %s /absolute/path/to/nxvk\n' "$0" >&2
    exit 2
fi

nxvk_root=$1
if [[ ! -d ${nxvk_root} ]]; then
    printf 'NXVK root does not exist: %s\n' "${nxvk_root}" >&2
    exit 2
fi
nxvk_root=$(cd -- "${nxvk_root}" && pwd)

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

cmake -S "${repo_root}/ports/switch_vulkan_probe" -B "${build_dir}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOOT3D_NXVK_ROOT="${nxvk_root}"

cmake --build "${build_dir}" \
    --target oot3d_switch_vulkan_probe_nro \
    -j "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"

nro=${build_dir}/oot3d_switch_vulkan_probe.nro
elf=${build_dir}/oot3d_switch_vulkan_probe.elf
if [[ ! -s ${nro} || ! -s ${elf} ]]; then
    printf 'Switch Vulkan probe outputs are missing.\n' >&2
    exit 1
fi
if [[ $(xxd -p -l 4 -s 16 "${nro}") != 4e524f30 ]]; then
    printf 'invalid NRO0 header: %s\n' "${nro}" >&2
    exit 1
fi
if ! "${DEVKITA64}/bin/aarch64-none-elf-readelf" -h "${elf}" | \
        grep -q 'Machine:.*AArch64'; then
    printf 'probe ELF is not AArch64: %s\n' "${elf}" >&2
    exit 1
fi

printf 'Switch Vulkan probe NRO: %s\n' "${nro}"
sha256sum "${nro}"
printf '%s\n' \
    'Licence note: this standalone NRO statically links the supplied NXVK.' \
    'Review that dependency GPL-2.0-or-later terms before distribution.'
