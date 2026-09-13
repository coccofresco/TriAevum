#!/usr/bin/env bash
set -euo pipefail

# Publisher-only: tools are acquired separately, never by an end-user install.
if [[ $# != 5 ]]; then
    echo "Usage: $0 AUDITED_PACKAGE NEW_APPDIR OUTPUT.AppImage APPIMAGETOOL RUNTIME" >&2
    exit 2
fi
package="$(realpath "$1")"
appdir="$(realpath -m "$2")"
output="$(realpath -m "$3")"
tool="$(realpath "$4")"
runtime="$(realpath "$5")"
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test ! -e "$appdir" && test ! -L "$appdir"
test ! -e "$output" && test ! -L "$output"
test -x "$tool"
case "$output" in "$appdir"/*|"$package"/*) echo 'Output must be outside package and AppDir' >&2; exit 2 ;; esac
printf '%s  %s\n' \
    a6d71e2b6cd66f8e8d16c37ad164658985e0cf5fcaa950c90a482890cb9d13e0 "$tool" \
    1cc49bcf1e2ccd593c379adb17c9f85a36d619088296504de95b1d06215aebbf "$runtime" | sha256sum --check --strict
export PYTHONPATH="$repo${PYTHONPATH:+:$PYTHONPATH}"
python3 -m tools.triaevum_release.appimage_package --package "$package" --output "$appdir"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$tool" --no-appstream \
    --runtime-file "$runtime" --mksquashfs-opt -processors --mksquashfs-opt 2 \
    "$appdir" "$output"
sha256sum "$output"
