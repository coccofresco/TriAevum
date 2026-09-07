# OOT3D whole-AOT port for Nintendo Switch homebrew

## Product boundary

The Windows product is the authoritative gameplay implementation. A32 is an
offline input to the whole-AOT generator; it is not executed by the shipped
runtime. The immutable product contract contains 12,419 native functions,
three audited host boundaries and zero residual A32 entries.

The Switch port therefore recompiles the same generated C++ shards for ARM64.
It must never link the Windows x64 archive, an A32 interpreter or a packed-op
fallback. `tools/oot3d/source_native_runtime`, the source imports and their
typed reconstructions are documentation and analysis material only. Product
mode rejects every source-native launch option and does not install the native
candidate callback. Gameplay remains the original whole-AOT graph.

Shared optimization evidence and the Windows adoption protocol are recorded in
the [cross-platform performance status](OOT3D_CROSS_PLATFORM_PERFORMANCE_STATUS.md).

## First portability gate

`ports/whole_aot` is deliberately isolated from the desktop application and
renderer dependency graph. It validates the product and generated manifests,
hashes all 256 generated shards, enforces a 64-bit little-endian IEEE-754
target and builds a native static archive. Exact gameplay semantics override
the root project's historical `-ffast-math` Switch flags.
The archive also overrides the toolchain's unconditional Release `-g` with
`-g0`; diagnostics belong to the small host executable, not to hundreds of
megabytes of generated gameplay code.

The generated directory and `aot_program.json` are local build products and
remain outside Git. Build the ARM64 archive with:

```sh
scripts/switch/build-whole-aot.sh \
  /path/to/cache-key/cpp \
  /path/to/cache-key/aot_program.json \
  /path/to/canonical-lf/whole_aot_functions.json \
  /path/to/exefs/code.bin \
  /path/to/exheader.bin \
  /path/to/nlohmann-json/include
```

The selection path is intentionally explicit. The repository-normalized LF
file is the canonical product input and has SHA-256
`e396db3f1d0785906502a6e2dfa871b33bb3bd250218cb40050ef74995a33be6`.
The historical Windows cache used semantically identical CRLF JSON with hash
`975ead42a936ad6b8a598a26051a33bf6fe7abf8b5e22fcfa618836e37250ff6`;
it is not a current product artifact. All platforms must regenerate from the
tracked LF selection rather than reuse that old cache.

Successful output is
`build-switch-whole-aot/liboot3d_native_whole_aot_portable.a`. Every object in
that archive is generated native ARM64 code; guest addresses and architectural
state are data contracts, not interpreted instructions.

For a stronger archive gate, `build-whole-aot-probe.sh` accepts the same six
arguments and links the complete dispatch table and every generated shard into
`oot3d_whole_aot_switch_probe.nro`. The libnx probe executes the generated
function at guest entry `0x00100000` for one block and validates the expected
block-limit continuation at `0x00100028`. It then writes
`sdmc:/switch/oot3dre/whole-aot-probe.txt`. Its architecture support contains
checked guest memory and exact floating-point operations only: no interpreter
core or decoded runtime dispatcher is compiled. Run it with
`run-whole-aot-probe-eden.sh EDEN_APPIMAGE PROBE_NRO`; success requires a fresh
receipt written from inside the emulated Switch guest.

## Complete homebrew product

`build-product.sh` first updates the portable ARM64 archive and then links the
process image, CTR services, input, audio, PICA frontend and savestate runtime.
The currently buildable Switch renderer is SDL2 plus Mesa/EGL/OpenGL.
Vulkan/NRI remains available to desktop targets but is explicitly disabled for
this homebrew configuration.  The separate
[Citra/Azahar donor audit](OOT3D_SWITCH_CITRA_AZAHAR_DONOR_AUDIT.md) defines an
isolated NXVK feasibility gate for reusing the qualified Linux Vulkan renderer;
it does not change this product boundary until that gate passes.

```sh
scripts/switch/build-product.sh \
  /path/to/cache-key/cpp \
  /path/to/cache-key/aot_program.json \
  /path/to/canonical-lf/whole_aot_functions.json \
  /path/to/exefs/code.bin \
  /path/to/exheader.bin \
  /path/to/nlohmann-json/include
```

The resulting files are `build-switch-product/oot3d_native_game.elf` and
`build-switch-product/oot3d_native_game.nro`. The build target runs the product
closure audit before linking.

The game extraction and TopScreen archive are deliberately not copied into
Git. Stage the NRO and the user-provided `code.bin`, `exheader.bin`, `romfs.bin`
and process manifest with:

```sh
scripts/switch/stage-product.sh \
  build-switch-product/oot3d_native_game.nro \
  /path/to/extracted/game \
  /path/to/sdmc
```

This produces `/switch/oot3dre` under the selected SD root. It also creates the
small `resources.o2r` containing the OpenGL shader required during renderer
initialization. When `OOT3D_TOPSCREEN_PACK` (or the optional fourth staging
argument) is absent, staging builds an O3TU v2 pack directly from the original
game RomFS and the official TopScreen 2.1.1 archive. Supply the archive through
the explicit `OOT3D_TOPSCREEN_211_ARCHIVE` path; alternatively pass an existing
O3TU v2 pack as the fourth staging argument or through `OOT3D_TOPSCREEN_PACK`.
For example:

```sh
OOT3D_TOPSCREEN_211_ARCHIVE=/path/to/topscreen211.zip \
scripts/switch/stage-product.sh \
  build-switch-product/oot3d_native_game.nro \
  /path/to/extracted/game /path/to/sdmc
```

The generated pack remains
under `build-switch-product`; no original game or mod payload enters Git.

With no homebrew arguments the NRO uses these paths:

- `sdmc:/switch/oot3dre/game/oot3d_native_process_manifest.json`
- `sdmc:/switch/oot3dre/resources.o2r`
- `sdmc:/switch/oot3dre/savedata`
- `sdmc:/switch/oot3dre/config/topscreen_ui.json`
- `sdmc:/switch/oot3dre/config/oot3d_controls.json`
- `sdmc:/switch/oot3dre/config/atlas_overrides.o3tu`

The Switch product selects TopScreen 2.1.1, native 30 Hz gameplay,
30 presentations per second and no visual interpolation. The latter is
intentional: interpolation has no intermediate presentation to produce at a
30 Hz output rate and retaining its delayed history would reintroduce the
ghosting reported by the Switch test.

The controller profile follows the labels printed on Joy-Con and Pro
Controllers rather than an Xbox-position remap. A/B/X/Y, L/R, ZL/ZR, +, - and
both sticks enter the ordinary native HID/Extra-HID contract. TopScreen policy
then assigns D-pad Up/Down to View/Ocarina. D-pad Left/Right select
Boomerang/Slingshot for child Link and Iron/Hover Boots for adult Link; ZL/ZR
remain the two 2.1.1 item lanes, avoiding the duplicated child mappings in the
desktop default. L/R retain native pause-page switching, + opens pause and -
retains the configured Select/Save action. No controller action is expressed
as a pointer coordinate.

## Eden verification

To stage directly into an isolated Eden state, pass
`STATE_DIR/data/eden/sdmc` as the third argument to `stage-product.sh`, then run:

```sh
scripts/switch/run-product-eden.sh EDEN_APPIMAGE STATE_DIR 40
```

The guest writes `sdmc:/switch/oot3dre/boot-status.txt`. A valid running receipt
must report 12,419 whole-AOT functions, three host boundaries, zero residual A32
entries and `stage=runtime_running`. The launcher additionally requires the
effective Switch product contract: `ui_profile=topscreen`,
`gameplay_timing=native30_no_interpolation`, `presentation_rate_hz=30`,
`visual_interpolation=0`, `control_profile=controller` and
`topscreen_211_assets=1`. The current runner also requires the 128-block
whole-AOT budget to have produced block-limit exits and requires the OpenGL
geometry cache to report at least one persistent draw, upload and resident
entry.

The integrated checkpoint produces an O3TU v2 pack with eight deduplicated
native CTXB overrides and both named 2.1.1 profile textures (`menu_atlas` and
`font_atlas`). Its SHA-256 is
`fd2d5f60cdb4618398f257b7cc945b79f635888bd3c03b8e9bbd2ed2189813a9`.
The last fully accepted pre-geometry-cache NRO SHA-256 was
`950fa574a51a3801f893055949aeb105f1be54e993e5f2ad6b6406c7ae720dd9`
(118,573,253 bytes). The current cache/budget candidate is 118,606,021 bytes,
SHA-256
`4091ded37b5524cba85ce19a67f7f4da09edd86a1cc98629cb3831d81b8f7b50`.
Focused TopScreen profile, Items hint, texture-runtime and native-input tests
all pass. Eden 0.2.1 clang-PGO remained live for 45 and 80 seconds; the latter
receipt reached 300/300 host/guest frames and confirmed every field above.

The geometry-cache candidate before the final receipt-only adjustment remained
live for a 60-second Eden 0.2.1 clang-PGO smoke. It reached
`stage=runtime_running`, 1/1 recorded host/guest frames, budget 128 and 5,186
block-limit exits. The strict runner rejected that run only because its receipt
was frozen at the first frame, before any cacheable geometry had appeared. The
current source writes once at the first frame and at most once more when the
first persistent geometry is observed; the current hash above still needs that
follow-up strict smoke. This bounded policy avoids periodic synchronous SD
writes in the presentation loop.

The OpenGL PICA path is also exercised directly by the Linux product because it
uses the same backend as Switch. A 180-presentation-frame regression run first
reproduced the Switch failure exactly. The corrected run renders the title
scene upright with the guest sky, clouds, stars, moon and terrain composition
matching the Vulkan reference. Its measured rate rose from 23.276 to 49.698
FPS with extended diagnostics enabled, then to 56.154 FPS after caching the
dedicated PICA VAO's attribute enables, divisors and pointer layouts. The main
fix is removal of a global
`glFinish()` from every PICA completion: draw inputs have already been copied
into backend-owned GL storage, and command ordering carries them through the
later display blit. Image barriers are now emitted only for shadow-producing
draws.

The backend now shares Vulkan's prepared-geometry registry and retains one GL
buffer for each cacheable geometry identity. A six-run paired Linux A/B raised
the median from 91.158468 to 94.811548 FPS (+4.01%) and reduced geometry upload
calls from 747,789 to 3,958 (-99.47%). All runs retained identical guest-state
fingerprints and draw counts. This directly exercises the code compiled for
Switch, but it is not physical-console performance evidence.

Two OpenGL/Vulkan convention errors caused the visual failure. Scanout now
compensates OpenGL's framebuffer origin before rotating the CTR physical
240x400 transfer into a logical 400x240 landscape image. OpenGL face winding
is no longer copied from Vulkan's positive-height viewport compensation; this
restores front-facing atmosphere and scene geometry instead of presenting the
back faces that looked like overlapping historical frames.

The pacing-fixed homebrew completed 60-, 80- and 100-second Eden 0.2.1
clang-PGO tests with fresh valid receipts, reaching 230/230, 640/640 and
820/820 host/guest frames as shader pipelines warmed. The subsequent NRO with
VAO state caching completed its own 80-second smoke test at 490 host/489 guest
frames and produced the same clean scene; Eden shader compilation makes these
short-run totals variable. Before the Switch pacing change, a 60-second run
produced 110 host/217 guest frames: a
slow presentation scheduled two full CTR refresh/render passes in an attempt
to catch up. Switch now drops that missed refresh and permits one guest refresh
per presentation, preventing the self-sustaining double-work spiral. This
changes no gameplay timing while the target 30 Hz rate is met; under load it
favors responsive, even presentation over accelerated catch-up.

`run-product-eden.sh` keeps a separate extracted AppDir for each AppImage.
Previously it silently reused the first extracted binary even when another
Eden version was requested. Eden nightly 0.5.9 currently crashes during system
initialization on this host, before the NRO starts; stable 0.2.1 and its
clang-PGO build run the product.

Eden currently logs unsupported/invalid Maxwell shader instructions while it
translates some pipelines emitted by the guest Mesa driver. The independent
`ports/switch_gl_probe` homebrew reproduces the same warnings while displaying
a correct OpenGL 4.3 colored triangle. That isolates the messages to the Eden
0.2.1/guest-Mesa baseline rather than the OOT3D PICA shader translator. The
product NRO remains alive and now presents a clean, oriented landscape scene.
Eden's x86-64 build must translate the NRO's AArch64 whole-AOT code and the
guest Mesa Maxwell stream; its pipeline compilation pauses remain visible in
short runs. Its raw startup frame count therefore cannot prove or disprove the
30 FPS physical-Switch target. The deterministic Kokiri Forest paired A/B
above is the current Linux OpenGL proxy. The receipt proves that Switch
requests exactly one non-interpolated 30 Hz guest refresh per 30 Hz
presentation. A
sustained physical-console trace remains the final performance acceptance
gate; the emulator evidence establishes boot, runtime, asset and cadence
correctness rather than physical-hardware throughput.

The geometry registry is bounded at 2,048 identities; the Kokiri A/B used 164
entries and observed no eviction. It is not yet byte-bounded. OpenGL PICA
texture and lighting-LUT caches remain unbounded, and an unmatched memory fill
can remain pending. Physical-hardware acceptance therefore still requires a
long memory/cardinality soak.

The guest's original PICA effects needed by the verified scene are present.
Desktop Vulkan-only enhancement passes such as CACAO, SSSR, TAA/upscaling and
the extended guide-buffer graph are not part of the Switch OpenGL backend; they
must not be described as missing guest effects or as already ported.

The Switch dispatcher now uses a conservative 128-block whole-AOT budget. A
block-limit exit unwinds the generated native call chain into the runtime
wrapper, which immediately resumes execution until the guest reaches a real
wait. It is a stack-safety boundary, not interpretation, guest scheduling or a
return to the SDL/Horizon event loop. A Linux sweep reduced exits from
1,989,424 at 64 blocks to 996,253 at 128 and improved that single sample by
3.93%; larger values did not show a stable additional gain. The 60-second Eden
run above exercised 128 without a crash, while physical-console stack/runtime
validation remains authoritative.

The renderer demonstration port is useful only as a platform/emulator smoke
test. It isolates the Maxwell warnings but is not accepted as gameplay evidence
for this product.
