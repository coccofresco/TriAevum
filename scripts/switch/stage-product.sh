#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)

if [[ $# -lt 2 || $# -gt 4 ]]; then
    printf 'usage: %s PRODUCT_NRO GAME_DIR [SD_ROOT] [TOPSCREEN_PACK]\n' \
        "$0" >&2
    exit 2
fi

nro=$1
game_dir=$2
sd_root=${3:-"${repo_root}/build-switch-product/sdmc"}
install_root=${sd_root}/switch/oot3dre
shader=${repo_root}/runtime/three_ds_recomp/src/fast/shaders/opengl/default.shader.glsl
topscreen_config=${repo_root}/config/topscreen_ui.switch.json
controls_config=${repo_root}/config/oot3d_controls.switch.json
topscreen_pack=${4:-${OOT3D_TOPSCREEN_PACK:-}}
topscreen_archive=${OOT3D_TOPSCREEN_211_ARCHIVE:-}

for input in "${nro}" "${game_dir}/code.bin" "${game_dir}/exheader.bin" \
             "${game_dir}/romfs.bin" \
             "${game_dir}/oot3d_native_process_manifest.json" "${shader}" \
             "${topscreen_config}" "${controls_config}"; do
    if [[ ! -s ${input} ]]; then
        printf 'required staging input is missing or empty: %s\n' "${input}" >&2
        exit 2
    fi
done

if [[ -z ${topscreen_pack} ]]; then
    if [[ -z ${topscreen_archive} || ! -s ${topscreen_archive} ]]; then
        printf '%s\n' \
            'TopScreen input is missing. Pass an O3TU v2 pack as argument 4,' \
            'set OOT3D_TOPSCREEN_PACK, or set OOT3D_TOPSCREEN_211_ARCHIVE' \
            'to the original TopScreen 2.1.1 zip.' >&2
        exit 2
    fi
    topscreen_pack=${repo_root}/build-switch-product/topscreen_2_1_1.o3tu
    python3 \
        "${repo_root}/tools/oot3d/decomp_support/scripts/build_topscreen_texture_override_pack.py" \
        --archive "${topscreen_archive}" \
        --original-romfs-image "${game_dir}/romfs.bin" \
        --output "${topscreen_pack}"
elif [[ ! -s ${topscreen_pack} ]]; then
    printf 'required TopScreen pack is missing or empty: %s\n' \
        "${topscreen_pack}" >&2
    exit 2
fi
pack_version=$(xxd -p -l 4 -s 4 "${topscreen_pack}")
if [[ ${pack_version} != 02000000 ]]; then
    printf 'Switch TopScreen pack must use O3TU v2: %s\n' \
        "${topscreen_pack}" >&2
    exit 2
fi

mkdir -p "${install_root}/config" "${install_root}/game" \
    "${install_root}/savedata"
install -m 0644 "${nro}" "${install_root}/oot3d_native_game.nro"
install -m 0644 "${game_dir}/code.bin" "${install_root}/game/code.bin"
install -m 0644 "${game_dir}/exheader.bin" "${install_root}/game/exheader.bin"
install -m 0644 "${game_dir}/romfs.bin" "${install_root}/game/romfs.bin"
install -m 0644 \
    "${game_dir}/oot3d_native_process_manifest.json" \
    "${install_root}/game/oot3d_native_process_manifest.json"
install -m 0644 "${topscreen_config}" \
    "${install_root}/config/topscreen_ui.json"
install -m 0644 "${controls_config}" \
    "${install_root}/config/oot3d_controls.json"
install -m 0644 "${topscreen_pack}" \
    "${install_root}/config/atlas_overrides.o3tu"

resource_work=$(mktemp -d)
trap 'rm -rf -- "${resource_work}"' EXIT
mkdir -p "${resource_work}/shaders/opengl"
install -m 0644 "${shader}" \
    "${resource_work}/shaders/opengl/default.shader.glsl"
(
    cd -- "${resource_work}"
    cmake -E tar cf resources.o2r --format=zip shaders
)
install -m 0644 "${resource_work}/resources.o2r" \
    "${install_root}/resources.o2r"

printf 'Switch SD layout staged at: %s\n' "${install_root}"
find "${install_root}" -maxdepth 2 -type f -printf '%P %s bytes\n' | sort
