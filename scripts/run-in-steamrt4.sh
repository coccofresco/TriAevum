#!/usr/bin/env bash
set -euo pipefail

# Publisher-only build environment. Nothing is installed on the host system.
if [[ $# -lt 3 ]]; then
  printf 'Usage: bash %s SDK_ROOT WORK_DIRECTORY COMMAND [ARGUMENTS...]\n' "$0" >&2
  exit 2
fi
sdk="$(realpath "$1")"
work="$(realpath "$2")"
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
shift 2
command -v bwrap >/dev/null
test -x "$sdk/usr/bin/clang++"
test -d "$work"
if [[ "$sdk" == / || "$work" == / || "$sdk/" == "$work/"* ||
      "$work/" == "$sdk/"* || "$root/" == "$work/"* || "$work/" == "$root/"* ]]; then
  printf 'Use a dedicated extracted SDK and a separate build directory.\n' >&2
  exit 2
fi
mkdir -p "$work/home"
bindings=()
for directory in usr etc var; do
  test -d "$sdk/$directory"
  bindings+=(--ro-bind "$sdk/$directory" "/$directory")
done
for link in bin sbin lib lib32 lib64 libx32; do
  test -L "$sdk/$link"
  bindings+=(--symlink "$(readlink "$sdk/$link")" "/$link")
done
if [[ -n "${TRIAEVUM_TRANSLATED_TITLE_DIR:-}" ]]; then
  title="$(realpath "$TRIAEVUM_TRANSLATED_TITLE_DIR")"
  test -f "$title/TITLE_SOURCE_MANIFEST.json"
  bindings+=(--ro-bind "$title" /home/title-source)
fi
exec bwrap --die-with-parent --unshare-user --uid 0 --gid 0 \
  --tmpfs / --tmpfs /home "${bindings[@]}" \
  --ro-bind "$root" /home/src --bind "$work" /home/work \
  --proc /proc --dev /dev --tmpfs /tmp \
  --ro-bind /etc/resolv.conf /etc/resolv.conf \
  --clearenv --setenv HOME /home/work/home \
  --setenv PATH /usr/bin:/bin:/usr/sbin:/sbin --setenv LANG C.UTF-8 \
  --setenv CMAKE_BUILD_PARALLEL_LEVEL 3 --chdir /home/work "$@"
