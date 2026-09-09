#!/usr/bin/env bash
set -euo pipefail

# Fold a recorded play session (run-linux-title.sh with TRIAEVUM_SHADER_INVENTORY=1,
# window closed normally) into a candidate AOT shader pack and prewarm manifest
# under RUNTIME_BUILD/pica-corpus. The checked-in baseline is read, never written.
# Cumulative: the candidate inventory and manifest grow with every session.
#
# Validate before promoting: launch with TRIAEVUM_PICA_CORPUS=1 and
# TRIAEVUM_PICA_STRICT=1 and replay the recorded areas; any miss aborts the run.
# Promote by copying pica-corpus/shader-inventory.json over
# tools/oot3d/native_pica_frontend/profiles/oot3d_pica_boot_title_kokiri_pipeline.json
# once the strict replay and a framebuffer comparison against dynamic generation
# pass (docs/OOT3D_NRI_PICA_AOT_RENDERER_PARITY.md, "Extending coverage").
if [[ $# -ne 2 ]]; then
  printf 'Usage: bash %s INSTALLATION RUNTIME_BUILD\n' "$0" >&2
  exit 2
fi
installation="$(realpath "$1")"
runtime="$(realpath "$2")"
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
captures="$installation/linux-captures"
baseline="$root/tools/oot3d/native_pica_frontend/profiles/oot3d_pica_boot_title_kokiri_pipeline.json"
corpus="$runtime/pica-corpus"
archive="$corpus/sessions/$(date +%Y%m%d-%H%M%S)"

test -x "$runtime/oot3d_native_pica_aot_compiler"
test -x "$runtime/oot3d_native_pica_pipeline_manifest"
test -f "$captures/shader-inventory.json"
mkdir -p "$archive"

inventories=(--inventory "$baseline")
[[ -f "$corpus/shader-inventory.json" ]] && inventories+=(--inventory "$corpus/shader-inventory.json")
"$runtime/oot3d_native_pica_aot_compiler" \
  "${inventories[@]}" --inventory "$captures/shader-inventory.json" \
  --pack "$corpus/oot3d_pica_corpus.o3ps" --manifest "$corpus/oot3d_pica_corpus_manifest.json" \
  --merged-inventory "$corpus/shader-inventory.json.new"
mv "$corpus/shader-inventory.json.new" "$corpus/shader-inventory.json"

if [[ -f "$captures/pipeline-inventory.json" ]]; then
  pipelines=()
  [[ -f "$corpus/oot3d_pica_corpus_pipelines.json" ]] && pipelines+=(--inventory "$corpus/oot3d_pica_corpus_pipelines.json")
  "$runtime/oot3d_native_pica_pipeline_manifest" \
    "${pipelines[@]}" --inventory "$captures/pipeline-inventory.json" \
    --output "$corpus/oot3d_pica_corpus_pipelines.json.new"
  mv "$corpus/oot3d_pica_corpus_pipelines.json.new" "$corpus/oot3d_pica_corpus_pipelines.json"
fi
# Archive the session so it is never merged twice and never lost.
mv "$captures/shader-inventory.json" "$archive/"
[[ -f "$captures/pipeline-inventory.json" ]] && mv "$captures/pipeline-inventory.json" "$archive/"
printf 'Session archived to %s\n' "$archive"
