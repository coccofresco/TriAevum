# ThreeDsRecomp Runtime

Reusable platform and rendering runtime for native Nintendo 3DS recompilation
projects. The current integration provides:

- SDL window, input and audio services;
- native PICA submission and texture decoding;
- OpenGL and NRI/Vulkan rendering;
- renderer extension contracts for scene-aware advanced effects;
- configuration, diagnostics and archive services used by title hosts.

The primary CMake target is `three_ds_recomp_runtime`, with the namespaced alias
`ThreeDsRecomp::Runtime`. Renderer modules use the `ThreeDsRecomp::*` aliases
and options use the `THREE_DS_RECOMP_*` prefix.

The `Ship::` and Fast3D surfaces are retained compatibility adapters. New code
must depend on the neutral renderer and host-service contracts; compatibility
owners are removed as consumers migrate.

## Derivation and license

This runtime is derived from libultraship. Its history and MIT copyright are
preserved, and the original license remains in `LICENSE`. Additional bundled
notices are listed in `THIRD_PARTY_NOTICES.md` and in the source files to which
they apply.
