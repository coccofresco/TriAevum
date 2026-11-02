#!/bin/bash
# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# Publisher entry for the complete, audited Forge app. See --help and
# docs/TRIAEVUM_MACOS_RELEASE.md. Existing output directories are not overwritten.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
exec "${TRIAEVUM_PUBLISHER_PYTHON:-python3}" \
    "$root/tools/triaevum_release/macos_forge_package.py" "$@"
