#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to Android NDK r29}"
: "${TRIAEVUM_ANDROID_RENDERER_BUILD_DIR:?Use a dedicated Android renderer build directory}"
cmake -S "$root/ports/android/renderer" -B "$TRIAEVUM_ANDROID_RENDERER_BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$TRIAEVUM_ANDROID_RENDERER_BUILD_DIR" \
    --target triaevum_android_renderer_link_probe --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
