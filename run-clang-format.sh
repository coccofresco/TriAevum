#!/usr/bin/env bash
set -euo pipefail

CLANG_FORMAT="${CLANG_FORMAT:-clang-format-14}"

# PRs check changed lines without rewriting unrelated release sources or their
# provenance manifests. With no base argument, retain the full-tree formatter.
if [[ $# -gt 1 ]]; then
    echo "usage: $0 [BASE_REF]" >&2
    exit 2
fi
if [[ $# -eq 1 ]]; then
    GIT_CLANG_FORMAT="${GIT_CLANG_FORMAT:-git-clang-format-14}"
    base=$(git rev-parse --verify "$1^{commit}")
    files=()
    while IFS= read -r -d '' file; do
        case "$file" in
            */generated/*|*/third_party/*|*/extern/*) continue ;;
        esac
        case "$file" in
            *.c|*.cpp|*.h|*.hpp) files+=("$file") ;;
        esac
    done < <(git diff --name-only --diff-filter=ACMR -z "$base" -- tools/oot3d runtime/three_ds_recomp)
    if [[ ${#files[@]} -gt 0 ]]; then
        "$GIT_CLANG_FORMAT" --binary "$CLANG_FORMAT" "$base" -- "${files[@]}"
    fi
    exit 0
fi

find tools/oot3d runtime/three_ds_recomp \
    -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
    ! -path '*/generated/*' \
    ! -path '*/third_party/*' \
    ! -path '*/extern/*' \
    -print0 | xargs -0 "$CLANG_FORMAT" -i --verbose
