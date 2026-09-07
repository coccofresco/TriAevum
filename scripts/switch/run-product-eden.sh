#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    printf 'usage: %s EDEN_APPIMAGE STATE_DIR [SECONDS]\n' "$0" >&2
    exit 2
fi

eden=$1
state_root=$2
run_seconds=${3:-40}
data_root=${state_root}/data/eden
install_root=${data_root}/sdmc/switch/oot3dre
nro=${install_root}/oot3d_native_game.nro
receipt=${install_root}/boot-status.txt
run_log=${state_root}/eden-product-run.log

if [[ ! -x ${eden} || ! -s ${nro} ]]; then
    printf 'Eden launcher or staged product NRO is missing.\n' >&2
    exit 2
fi

eden_exec=${eden}
eden_subcommand=()
eden_appdir=
if [[ ${eden} == *.AppImage ]]; then
    # Run the bundled CLI directly.  This bypasses the graphical AppImage
    # self-updater prompt, which otherwise outlives headless timeout runs.  A
    # persistent extraction also avoids spending longer deleting the AppImage
    # payload than executing a short smoke test.
    # Keep extractions version-specific. Reusing one fixed AppDir silently ran
    # an older Eden binary even when the caller selected a newer AppImage.
    eden_name=$(basename -- "${eden%.AppImage}")
    eden_extract=${state_root}/eden-appimage-${eden_name}
    eden_appdir=${eden_extract}/squashfs-root
    if [[ ! -x ${eden_appdir}/bin/eden-cli ]]; then
        mkdir -p "${eden_extract}"
        (
            cd -- "${eden_extract}"
            "${eden}" --appimage-extract >/dev/null
        )
    fi
    eden_exec=${eden_appdir}/bin/eden-cli
elif [[ $(basename -- "${eden}") != eden-cli ]]; then
    eden_subcommand=(eden-cli)
fi
if [[ ! ${run_seconds} =~ ^[1-9][0-9]*$ ]]; then
    printf 'SECONDS must be a positive integer.\n' >&2
    exit 2
fi

mkdir -p \
    "${data_root}/amiibo" \
    "${data_root}/dump" \
    "${data_root}/keys" \
    "${data_root}/load" \
    "${data_root}/log" \
    "${data_root}/nand" \
    "${data_root}/play_time" \
    "${data_root}/screenshots" \
    "${data_root}/shader" \
    "${data_root}/tas" \
    "${state_root}/config" \
    "${state_root}/cache"
rm -f -- "${receipt}"

set +e
env \
    APPDIR="${eden_appdir}" \
    PATH="${eden_appdir:+${eden_appdir}/bin:}${PATH}" \
    XDG_DATA_HOME="${state_root}/data" \
    XDG_CONFIG_HOME="${state_root}/config" \
    XDG_CACHE_HOME="${state_root}/cache" \
    timeout --foreground --kill-after=5s "${run_seconds}s" \
        "${eden_exec}" "${eden_subcommand[@]}" --game "${nro}" \
        >"${run_log}" 2>&1
eden_status=$?
set -e

if (( eden_status != 124 && eden_status != 137 )); then
    printf 'Eden stopped before the requested timeout (status %d).\n' \
        "${eden_status}" >&2
    tail -n 80 "${run_log}" >&2
    exit 1
fi
if [[ ! -f ${receipt} ]] ||
   ! grep -qx 'stage=runtime_running' "${receipt}" ||
   ! grep -qx 'whole_aot_functions=12419' "${receipt}" ||
   ! grep -qx 'host_boundaries=3' "${receipt}" ||
   ! grep -qx 'residual_a32=0' "${receipt}" ||
   ! grep -qx 'ui_profile=topscreen' "${receipt}" ||
   ! grep -qx 'gameplay_timing=native30_no_interpolation' "${receipt}" ||
   ! grep -qx 'presentation_rate_hz=30' "${receipt}" ||
   ! grep -qx 'visual_interpolation=0' "${receipt}" ||
   ! grep -qx 'control_profile=controller' "${receipt}" ||
   ! grep -qx 'topscreen_211_assets=1' "${receipt}" ||
   ! grep -qx 'whole_aot_block_budget=128' "${receipt}" ||
   ! grep -Eq '^whole_aot_block_limit_exits=[1-9][0-9]*$' \
       "${receipt}" ||
   ! grep -qx 'pica_geometry_cache_enabled=1' "${receipt}" ||
   ! grep -Eq '^pica_geometry_persistent_draws=[1-9][0-9]*$' \
       "${receipt}" ||
   ! grep -Eq '^pica_geometry_persistent_uploads=[1-9][0-9]*$' \
       "${receipt}" ||
   ! grep -Eq '^pica_geometry_registry_entries=[1-9][0-9]*$' \
       "${receipt}"; then
    printf 'Eden did not produce a valid runtime receipt: %s\n' \
        "${receipt}" >&2
    tail -n 80 "${run_log}" >&2
    exit 1
fi

printf 'Eden whole-AOT product remained live for %s seconds.\n' \
    "${run_seconds}"
printf 'Eden log: %s\n' "${run_log}"
cat "${receipt}"
