#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    printf 'usage: %s EDEN_APPIMAGE PROBE_NRO [STATE_DIR]\n' "$0" >&2
    exit 2
fi

eden=$1
nro=$2
state_root=${3:-"${PWD}/build-switch-whole-aot/eden-probe-state"}

if [[ ! -x ${eden} || ! -s ${nro} ]]; then
    printf 'Eden launcher or probe NRO is missing.\n' >&2
    exit 2
fi

data_root=${state_root}/data/eden
sd_root=${data_root}/sdmc
receipt=${sd_root}/switch/oot3dre/whole-aot-probe.txt
mkdir -p \
    "${data_root}/amiibo" \
    "${data_root}/dump" \
    "${data_root}/keys" \
    "${data_root}/load" \
    "${data_root}/log" \
    "${data_root}/nand" \
    "${data_root}/screenshots" \
    "${data_root}/shader" \
    "${data_root}/tas" \
    "${sd_root}/switch/oot3dre" \
    "${state_root}/config" \
    "${state_root}/cache"
rm -f -- "${receipt}"

set +e
env \
    XDG_DATA_HOME="${state_root}/data" \
    XDG_CONFIG_HOME="${state_root}/config" \
    XDG_CACHE_HOME="${state_root}/cache" \
    "${eden}" eden-cli --game "${nro}"
eden_status=$?
set -e

if (( eden_status != 0 )); then
    printf 'Eden failed while running the whole-AOT probe (status %d).\n' \
        "${eden_status}" >&2
    exit "${eden_status}"
fi
if [[ ! -f ${receipt} ]] ||
   ! grep -qx 'status=passed' "${receipt}" ||
   ! grep -qx 'native_functions=12419' "${receipt}" ||
   ! grep -qx 'host_boundaries=3' "${receipt}" ||
   ! grep -qx 'residual_a32=0' "${receipt}" ||
   ! grep -qx 'dispatch_entry_points=161332' "${receipt}"; then
    printf 'Eden exited without a valid whole-AOT guest receipt: %s\n' \
        "${receipt}" >&2
    exit 1
fi

printf 'Eden whole-AOT ARM64 probe passed: %s\n' "${receipt}"
