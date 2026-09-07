#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    printf 'usage: %s PRODUCT_PACKAGE [SECONDS]\n' "$0" >&2
    exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)
package_root=$1
run_seconds=${2:-}
build_root=${repo_root}/build-linux-product
run_root=${build_root}/run
game_root=${package_root}/game
binary=${build_root}/oot3d_native_game
manifest=${game_root}/oot3d_native_process_manifest.json
resources=${package_root}/resources
shader_pack=${build_root}/oot3d_pica_default.o3ps
tinyxml_lib=${repo_root}/build-linux-deps/tinyxml2/usr/lib

for required in "${binary}" "${manifest}" "${resources}" \
    "${shader_pack}" "${run_root}/config/oot3d_native_game.json" \
    "${run_root}/config/oot3d_controls.json" \
    "${run_root}/config/topscreen_ui.json" \
    "${run_root}/config/atlas_overrides.o3tu"; do
    if [[ ! -e ${required} ]]; then
        printf 'required Linux runtime input is missing: %s\n' "${required}" >&2
        exit 2
    fi
done
if [[ -n ${run_seconds} && ! ${run_seconds} =~ ^[1-9][0-9]*$ ]]; then
    printf 'SECONDS must be a positive integer.\n' >&2
    exit 2
fi

mkdir -p "${run_root}/savedata"
arguments=(
    --a32-process-manifest "${manifest}"
    --resource-root "${resources}"
    --config "${run_root}/config/oot3d_native_game.json"
    --controls-config "${run_root}/config/oot3d_controls.json"
    --ui-profile topscreen
    --topscreen-config "${run_root}/config/topscreen_ui.json"
    --topscreen-texture-overrides "${run_root}/config/atlas_overrides.o3tu"
    --save-data "${run_root}/savedata"
    --quick-state "${run_root}/savedata/quick.oot3dsav"
    --pica-aot-shader-pack "${shader_pack}"
    --renderer vulkan
    --gameplay-timing native30_interpolated
    --presentation-rate 60
    --width 1280
    --height 720
)
if [[ -n ${run_seconds} ]]; then
    arguments+=(--max-seconds "${run_seconds}")
fi

cd -- "${run_root}"
exec env LD_LIBRARY_PATH="${tinyxml_lib}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
    "${binary}" "${arguments[@]}"
