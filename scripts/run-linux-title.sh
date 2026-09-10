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
python3 "$root/tools/triaevum_release/linux_desktop.py"

cd "$installation"
mkdir -p linux-captures
python3 "$root/tools/triaevum_release/prepare_linux_launch.py" \
  "$installation" "$title/triaevum_title_aot.so"
# Default: the checked-in baseline pack. TRIAEVUM_PICA_CORPUS=1 selects the
# local candidate built by scripts/extend-pica-corpus.sh (never the baseline).
pack="$runtime/oot3d_pica_default.o3ps"
pipelines=""
if [[ "${TRIAEVUM_PICA_CORPUS:-0}" == 1 ]]; then
  pack="$runtime/pica-corpus/oot3d_pica_corpus.o3ps"
  pipelines="$runtime/pica-corpus/oot3d_pica_corpus_pipelines.json"
  test -f "$pack"
fi
shader_args=()
# Forge-generated profiles may already carry the shipped pack (and prewarm
# manifest); the runtime rejects duplicate options, so add ours only when the
# profile has none and refuse a profile that already has more than one.
pack_options=$(python3 - "$installation/TriAevum.linux.launch.json" <<'EOF'
import json, sys
try:
    print(json.load(open(sys.argv[1]))["arguments"].count("--pica-aot-shader-pack"))
except (OSError, ValueError, KeyError, AttributeError) as exc:
    sys.exit(f"Launch profile is unusable: {exc}")
EOF
) || exit 2
if [[ "$pack_options" -gt 1 ]]; then
  printf 'Launch profile lists --pica-aot-shader-pack %s times\n' "$pack_options" >&2
  exit 2
fi
if [[ -f "$pack" && "$pack_options" == 0 ]]; then
  shader_args+=(--pica-aot-shader-pack "$pack")
fi
# Strict: any shader outside the pack aborts the run. Validation replay only.
if [[ "${TRIAEVUM_PICA_STRICT:-0}" == 1 ]]; then
  shader_args+=(--pica-aot-shader-strict)
fi
# Record every effective PICA shader and pipeline so the AOT pack and prewarm
# manifest can be extended past the boot/title/Kokiri baseline
# (see docs/OOT3D_NRI_PICA_AOT_RENDERER_PARITY.md). Written at renderer
# shutdown: close the window normally, do not SIGTERM.
if [[ "${TRIAEVUM_SHADER_INVENTORY:-0}" == 1 ]]; then
  shader_args+=(--pica-effective-shader-inventory
    "$installation/linux-captures/shader-inventory.json"
    --pica-pipeline-inventory
    "$installation/linux-captures/pipeline-inventory.json")
fi
# Create every manifest pipeline before the first frame instead of on first use.
# Requires the candidate corpus: manifest and pack come from the same sessions.
if [[ "${TRIAEVUM_PIPELINE_PREWARM:-0}" == 1 ]]; then
  test -f "$pipelines"
  shader_args+=(--pica-pipeline-manifest "$pipelines" --pica-pipeline-prewarm)
fi
# Overlap guest execution with rendering on a second thread.
if [[ "${TRIAEVUM_GUEST_THREAD:-0}" == 1 ]]; then
  shader_args+=(--guest-thread)
fi
capture_args=()
# Readback deliberately stalls the device. Interactive previews must not pay
# for periodic captures; opt in explicitly when collecting image evidence.
if [[ "${TRIAEVUM_CAPTURE_FRAMES:-0}" == 1 ]]; then
  capture_args+=(--screenshot "$installation/linux-captures/framebuffer.bmp"
    --screenshot-start-frame 120 --screenshot-interval 300 --screenshot-sequence)
fi
# Keep the copied Windows profile unchanged; duplicate plugin options are forbidden.
# stdout is block-buffered into the tee pipe; without misses to fill it the
# progress lines never appear, so line-buffer it.
timeout --signal=TERM --kill-after=5 "$((seconds + 30))" \
  stdbuf -oL "$runtime/TriAevum" --launch-profile "$installation/TriAevum.linux.launch.json" \
  "${shader_args[@]}" \
  --frames 0 --max-seconds "$seconds" \
  --output "$installation/linux-captures/runtime.json" \
  "${capture_args[@]}" \
  2>&1 | tee "$installation/linux-captures/launch.log"
