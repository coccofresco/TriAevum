#!/usr/bin/env bash
set -euo pipefail

# Publisher/developer build only. This does not create a playable APK.
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ndk="${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to the extracted Android NDK}"
build="${TRIAEVUM_BUILD_DIR:-$root/../triaevum-android-build}"
jobs="${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
test -f "$ndk/build/cmake/android.toolchain.cmake"
args=(
  -S "$root/ports/android/native" -B "$build" -G Ninja
  "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake"
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29
  -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
  "-DTRIAEVUM_TRANSLATED_TITLE_DIR=${TRIAEVUM_TRANSLATED_TITLE_DIR:-}"
)
targets=(
  triaevum_module native_presentation_policy_tests
  triaevum_android_abi_probe triaevum_android_nri_probe
  oot3d_native_direct_aot_tests triaevum_probe_empty_title
  triaevum_runtime_layout_tests triaevum_input_service_tests
  triaevum_audio_service_tests triaevum_filesystem_service_tests
  triaevum_native_module_loader_tests
)
if [[ "${TRIAEVUM_BUILD_TITLE:-0}" == 1 ]]; then
  : "${TRIAEVUM_TRANSLATED_TITLE_DIR:?Title builds require verified translated sources}"
  targets+=(triaevum_android_title)
fi
cmake "${args[@]}" "$@"
cmake --build "$build" --parallel "$jobs" --target "${targets[@]}"
printf 'Android ARM64 native build completed: %s\nNot an APK or an on-device gameplay qualification.\n' "$build"
