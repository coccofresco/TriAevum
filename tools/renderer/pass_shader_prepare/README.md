# Renderer Pass Shader Preparation

Small developer build of the same compiler used by Forge. It has no title,
window, GPU or NRI dependency. Shader sources and compile options are shared
with the live renderer; the output is the existing persistent SPVC v2 cache.

Dependencies: CMake 3.30+, C++20, nlohmann_json CMake package, shared shaderc.
Python is needed only for the optional integration tests. Build on the host
that will run the tool; do not run cross-compiled executables during preparation.

```sh
cmake -S tools/renderer/pass_shader_prepare -B build/pass-shader-prepare \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=DEPENDENCY_PREFIX
cmake --build build/pass-shader-prepare --config Release --parallel 3
ctest --test-dir build/pass-shader-prepare -C Release --output-on-failure
```

On Windows also set `SHADERC_RUNTIME` to the matching `shaderc_shared.dll`,
`SHADERC_LIBRARY` to its import library and `SHADERC_INCLUDE_DIR` as needed.
The build deploys the DLL beside the tool; no SDK is required by end users.

```text
oot3d_native_pica_aot_compiler --prepare-renderer-cache USER_DATA/cache/renderer
  --manifest PRIVATE_REPORT.json
```

The CLI and Forge take the common renderer cache root; `SpirvCache` appends
`spirv-v2` itself. Preparation requires a compatible bundled
compiler identity; an executable/library mismatch intentionally misses instead
of accepting unknown shader output. Normal installation calls this stage before
device pipeline preparation and never boots a game or compiles title code.

See `docs/TRIAEVUM_FORGE_SHADER_HANDOFF.md` for private live qualification and
packaging boundaries. Do not publish game-derived captures, inventories or packs.
