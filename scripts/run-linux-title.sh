#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 || $# -gt 4 ]]; then
  printf 'Usage: bash %s INSTALLATION RUNTIME_BUILD TITLE_BUILD [SECONDS]\n' "$0" >&2
  exit 2
fi
installation="$(realpath "$1")"
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
runtime="$(realpath "$2")"
title="$(realpath "$3")"
seconds="${4:-60}"
if [[ ! "$seconds" =~ ^[1-9][0-9]*$ ]]; then
  printf 'SECONDS must be a positive integer\n' >&2
  exit 2
fi
test -x "$runtime/TriAevum"
test -f "$title/triaevum_title_aot.so"
test -f "$installation/TriAevum.launch.json"

# SSH has no desktop environment. Import only display/session routing, never eval.
while IFS= read -r assignment; do
  case "$assignment" in
    DISPLAY=*|WAYLAND_DISPLAY=*|XAUTHORITY=*|XDG_RUNTIME_DIR=*|DBUS_SESSION_BUS_ADDRESS=*)
      export "$assignment" ;;
  esac
done < <(systemctl --user show-environment)
if [[ -z "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ]]; then
  printf 'No active desktop display; log into the Linux desktop first\n' >&2
  exit 2
fi

cd "$installation"
mkdir -p linux-captures
python3 "$root/tools/triaevum_release/prepare_linux_launch.py" \
  "$installation" "$title/triaevum_title_aot.so"
shader_args=()
if [[ -f "$runtime/oot3d_pica_default.o3ps" ]]; then
  shader_args+=(--pica-aot-shader-pack "$runtime/oot3d_pica_default.o3ps")
fi
capture_args=()
# Readback deliberately stalls the device. Interactive previews must not pay
# for periodic captures; opt in explicitly when collecting image evidence.
if [[ "${TRIAEVUM_CAPTURE_FRAMES:-0}" == 1 ]]; then
  capture_args+=(--screenshot "$installation/linux-captures/framebuffer.bmp"
    --screenshot-start-frame 120 --screenshot-interval 300 --screenshot-sequence)
fi
# Keep the copied Windows profile unchanged; duplicate plugin options are forbidden.
timeout --signal=TERM --kill-after=5 "$((seconds + 30))" \
  "$runtime/TriAevum" --launch-profile "$installation/TriAevum.linux.launch.json" \
  "${shader_args[@]}" \
  --frames 0 --max-seconds "$seconds" \
  --output "$installation/linux-captures/runtime.json" \
  "${capture_args[@]}" \
  2>&1 | tee "$installation/linux-captures/launch.log"
