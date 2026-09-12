#!/usr/bin/env bash
set -euo pipefail

test -x /usr/bin/clang++
test -d "$HOME"
# The test must not modify the repository or the toolchain, even as mapped root.
if touch /home/src/.triaevum-write-probe 2>/dev/null; then
  printf 'Source checkout is writable\n' >&2
  exit 1
fi
if touch /usr/.triaevum-write-probe 2>/dev/null; then
  printf 'SDK is writable\n' >&2
  exit 1
fi
touch /home/work/.triaevum-write-probe
cmake -P /home/src/scripts/test-linux-distribution.cmake
printf 'Steam Runtime build isolation passed\n'
