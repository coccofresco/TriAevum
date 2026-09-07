# Forge whole-AOT v2

> Distribution update (2026-09-05): [Precompiled release contract](TRIAEVUM_PRECOMPILED_RELEASE.md)
> supersedes this document's user-side compilation and no-translated-title-code
> requirements. The material below remains developer/history context; users now
> import their ROM into a package containing precompiled title logic, without SDKs.

## Decision

The playable Forge path uses the mature `oot3d_native_game` host compiled with
`OOT3D_DIRECT_AOT_PLUGIN=ON`. The public package contains an empty ABI-compatible
`triaevum_title_aot.dll`; Forge replaces it locally with a private plugin built
from the user's verified title inputs. The old packed-operation ABI-v1 backend
remains a diagnostic fallback and must not be selected by normal setup.

## Private build

1. Verify and extract the decrypted `.3ds` or `.cci` input.
2. Audit the zero-residual structural program against `code.bin` and ExHeader.
3. Generate 256 affinity shards with `whole_aot_cpp.py`.
4. Compile missing shards with the content-addressed ThinLTO object cache.
5. Link the archive, ABI wrapper and title-neutral memory/VFP support into the
   private DLL.
6. Package compatibility metadata, atomically install the DLL, and write the
   launch profile with NRI/Vulkan and TopScreen enabled.

ABI v2 also keeps observable exits inside the plugin. Generated whole-AOT code
owns thread-local execution and snapshot state, so the host queries that state
and requests an exit through plugin callbacks instead of reading a second host
copy. This is required for per-frame TopScreen camera and item hooks to retain
the same behavior as the statically linked oracle.

Every stage is keyed by source, generator, toolchain and dependency hashes.
Changing configuration never recompiles title code; an unchanged title reuses
the final DLL.

## Validation on 2026-09-03

- Generated product: 12,419 functions, 256 shards, zero file-hash differences
  from the qualified whole-AOT product.
- Dynamic/static equivalence: memory content, memory state, process state, top
  framebuffer, bottom framebuffer, compiled-function and UI fingerprints all
  matched.
- Dynamic counters: 52,262 whole-AOT calls, 4,164,314 direct calls, 540,441
  external calls, zero unsupported exits, faults or block-limit exits.
- Throughput run: 300 measured frames in 4.99978 seconds (60.0026 fps, VSync
  and frame pacing disabled). The repaired installed package repeated at
  59.9788 fps over 120 measured frames after warm-up after the final ABI-v2
  observable-exit bridge was enabled.
- The launch report selected `ui_profile=topscreen`, loaded the explicit
  TopScreen configuration and reported `topscreen_profile_active=true`.

## Remaining release gate

The Windows pilot bundles LLVM and its builtins and is validated on the
development machine. A pristine-machine release still requires a separately
recorded test proving that the packaged C++/Windows link environment is fully
self-contained. Failure must be reported by Forge as a toolchain error; it must
never fall back silently to ABI v1.
