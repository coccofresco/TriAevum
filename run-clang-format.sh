#!/usr/bin/env bash
set -euo pipefail

CLANG_FORMAT="${CLANG_FORMAT:-clang-format-14}"

find tools/oot3d runtime/three_ds_recomp \
    -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
    ! -path '*/generated/*' \
    ! -path '*/third_party/*' \
    ! -path '*/extern/*' \
    -print0 | xargs -0 "$CLANG_FORMAT" -i --verbose
