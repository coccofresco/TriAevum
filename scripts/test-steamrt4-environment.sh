#!/usr/bin/env bash
set -euo pipefail

sdk="$(realpath "${1:?Specify SDK root}")"
work="$(realpath "${2:?Specify work directory}")"
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
for script in run-in-steamrt4 prepare-steamrt4-sdk steamrt4-sdk-dependencies; do
  bash -n "$root/scripts/$script.sh"
done
if bash "$root/scripts/run-in-steamrt4.sh" "$sdk" "$sdk" true; then
  printf 'Overlapping SDK/work paths were accepted\n' >&2
  exit 1
fi
bash "$root/scripts/run-in-steamrt4.sh" "$sdk" "$work" \
  bash /home/src/scripts/test-steamrt4-environment-inside.sh
