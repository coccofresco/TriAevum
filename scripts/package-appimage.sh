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
# Pinned to the tagged releases appimagetool 1.9.1 and type2-runtime 20251108
# (the earlier `continuous` assets were replaced upstream and are no longer
# acquirable; reviewed for v0.8.0-beta.1.2).
printf '%s  %s\n' \
    ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 "$tool" \
    2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d "$runtime" | sha256sum --check --strict
export PYTHONPATH="$repo${PYTHONPATH:+:$PYTHONPATH}"
python3 -m tools.triaevum_release.appimage_package --package "$package" --output "$appdir"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$tool" --no-appstream \
    --runtime-file "$runtime" --mksquashfs-opt -processors --mksquashfs-opt 2 \
    "$appdir" "$output"
sha256sum "$output"
