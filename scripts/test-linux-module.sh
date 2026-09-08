#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build="${TRIAEVUM_MODULE_BUILD_DIR:-$root/../triaevum-module-build}"
tests=(
  triaevum_module_tests triaevum_tam_metadata_tests
  triaevum_input_service_tests triaevum_audio_service_tests
  triaevum_filesystem_service_tests triaevum_service_registry_tests
  triaevum_pica_service_adapter_tests triaevum_pica_service_client_tests
  triaevum_pica_scanout_state_tests triaevum_guest_memory_lease_pool_tests
  triaevum_service_abi_c_tests
)
cmake -S "$root/runtime/triaevum_module" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build "$build" --target "${tests[@]}" triaevum_native_module_loader_tests \
  --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
for test in "${tests[@]}"; do
  "$build/$test"
  printf 'PASS %s\n' "$test"
done
"$build/triaevum_native_module_loader_tests" "$build/triaevum_mock_module.so"
printf 'PASS triaevum_native_module_loader_tests\n'
