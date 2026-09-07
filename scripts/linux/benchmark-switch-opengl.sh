#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 4 ]]; then
    printf 'usage: %s STAGED_SWITCH_ROOT [FRAMES] [WARMUP_FRAMES] [CHECKPOINT]\n' "$0" >&2
    exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)
install_root=$(realpath -- "$1")
frames=${2:-1200}
warmup_frames=${3:-300}
checkpoint=${4:-}
whole_aot_block_budget=${OOT3D_WHOLE_AOT_BLOCK_BUDGET:-}
disable_geometry_cache=${OOT3D_DISABLE_OPENGL_PICA_GEOMETRY_CACHE:-0}
if [[ -n ${checkpoint} ]]; then
    checkpoint=$(realpath -- "${checkpoint}")
fi
build_root=${repo_root}/build-linux-product
run_root=${build_root}/throughput-opengl
binary=${build_root}/oot3d_native_game
manifest=${install_root}/game/oot3d_native_process_manifest.json
resources=${install_root}/resources.o2r
controls=${install_root}/config/oot3d_controls.json
topscreen=${install_root}/config/topscreen_ui.json
overrides=${install_root}/config/atlas_overrides.o3tu
config_template=${repo_root}/config/oot3d_native_game.throughput.json
config=${run_root}/oot3d_native_game.json
summary=${run_root}/oot3d_switch_opengl_throughput.json
tinyxml_lib=${repo_root}/build-linux-deps/tinyxml2/usr/lib

if [[ ! ${frames} =~ ^[1-9][0-9]*$ ||
      ! ${warmup_frames} =~ ^[0-9]+$ ||
      ${warmup_frames} -ge ${frames} ]]; then
    printf 'FRAMES must be positive and WARMUP_FRAMES smaller than FRAMES.\n' >&2
    exit 2
fi

for required in "${binary}" "${manifest}" "${resources}" "${controls}" \
    "${topscreen}" "${overrides}" "${config_template}"; do
    if [[ ! -s ${required} ]]; then
        printf 'required throughput input is missing: %s\n' "${required}" >&2
        exit 2
    fi
done
if [[ -n ${checkpoint} && ! -s ${checkpoint} ]]; then
    printf 'checkpoint is missing: %s\n' "${checkpoint}" >&2
    exit 2
fi
if [[ -n ${whole_aot_block_budget} &&
      ! ${whole_aot_block_budget} =~ ^[1-9][0-9]*$ ]]; then
    printf 'OOT3D_WHOLE_AOT_BLOCK_BUDGET must be a positive integer.\n' >&2
    exit 2
fi
if [[ ${disable_geometry_cache} != 0 && ${disable_geometry_cache} != 1 ]]; then
    printf 'OOT3D_DISABLE_OPENGL_PICA_GEOMETRY_CACHE must be 0 or 1.\n' >&2
    exit 2
fi

mkdir -p "${run_root}/savedata"
install -m 0644 "${config_template}" "${config}"

arguments=(
    --a32-process-manifest "${manifest}"
    --resource-root "${resources}"
    --config "${config}"
    --controls-config "${controls}"
    --ui-profile topscreen
    --topscreen-config "${topscreen}"
    --topscreen-texture-overrides "${overrides}"
    --save-data "${run_root}/savedata"
    --quick-state "${run_root}/savedata/quick.oot3dsav"
    --renderer opengl
    --gameplay-timing native30_no_interpolation
    --throughput-benchmark
    --frames "${frames}"
    --benchmark-warmup-frames "${warmup_frames}"
    --disable-audio
    --width 1280
    --height 720
    --output "${summary}"
)
if [[ -n ${checkpoint} ]]; then
    arguments+=(--load-state "${checkpoint}")
fi
if [[ -n ${whole_aot_block_budget} ]]; then
    arguments+=(--whole-aot-block-budget "${whole_aot_block_budget}")
fi
if [[ ${disable_geometry_cache} == 1 ]]; then
    arguments+=(--disable-opengl-pica-geometry-cache)
fi
if [[ ${OOT3D_THROUGHPUT_PROFILE:-0} == 1 ]]; then
    # Runtime counters are intentionally independent from verbose diagnostic
    # histories: the latter builds large JSON/SVC strings in the hot path and
    # would distort the throughput profile it is meant to explain.
    arguments+=(--profile-a32-runtime)
fi

cd -- "${run_root}"
env \
    LD_LIBRARY_PATH="${tinyxml_lib}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
    OOT3D_GRAPHICS_FRAME_RATE=Uncapped \
    vblank_mode=0 \
    __GL_SYNC_TO_VBLANK=0 \
    "${binary}" "${arguments[@]}"

jq -e '
    .benchmark_window.throughput_mode == true and
    .benchmark_window.vsync == false and
    .benchmark_window.sdl_frame_limiter_enabled == false and
    .benchmark_window.pacing_enabled == false and
    .benchmark_window.measured_frames > 0 and
    .benchmark_window.frames_per_second > 0
' "${summary}" >/dev/null

jq -r '
    "OpenGL full-refresh throughput: \(.benchmark_window.frames_per_second) FPS " +
    "(\(.benchmark_window.measured_frames) frames, \(.benchmark_window.host_seconds) s)"
' "${summary}"
printf 'Summary: %s\n' "${summary}"
