# OoT3D private game-module vertical

## Closed boundary

`oot3d_game_module` is the first executable private-title module behind the
TriAevum v1 C ABI. It contains the audited 12,419-function whole-AOT product,
process state, 32-bit guest memory, CTR/SVC host semantics and title scheduling.
It deliberately contains no window, SDL, NRI, Vulkan, OpenGL, physical input
or audio-device implementation.

Graphics requests leave the module only through `TRIAEVUM_SERVICE_PICA_V1`;
PCM and abstract controls cross `TRIAEVUM_SERVICE_AUDIO_V1` and
`TRIAEVUM_SERVICE_INPUT_V1`. Process inputs, RomFS reads and save-data I/O
cross `TRIAEVUM_SERVICE_FILESYSTEM_V1`; the module receives logical content or
save paths and never opens a host path directly.
The service-only CTR host is built from the same source as the linked oracle,
with direct PICA frontend calls compile-time excluded. The resulting Windows
DLL exports only `TriAevumQueryModuleV1`; its only PE dependency is
`KERNEL32.dll`.

## Private packaging

Forge `package-module` combines a locally generated native image with the
verified recipe identity and physical-memory layout from `content.tap`. The
TAM is stored under a short, content-addressed `modules/<key>.tam` path. The
full target, translator, native-image, source and TAM hashes remain in
`forge-state.json`. This prevents both accidental overwrite and legacy Win32
path-length failures.

The TAM and extracted native image are private local artifacts. Neither may be
placed in a public package or source archive.

## Reproducible developer smoke

Build targets:

```text
oot3d_game_module
oot3d_game_module_smoke
```

After `forge prepare` and `forge package-module`, run:

```text
oot3d_game_module_smoke <game.tam> <module-cache> <content.tap> 120
```

The smoke executable links only the generic `triaevum_module` runtime. It
dynamically loads the TAM, registers typed audit PICA, audio, filesystem and
input services, starts the real title entrypoint, advances 120 native frames,
round-trips a portable save state and maps/unmaps a checked guest-memory lease.

Evidence recorded on 2026-09-02 for the supported EUR recipe:

- 12,419 compiled title functions, three audited host boundaries and zero
  residual A32 entries;
- 120 module frames completed in about 2.5 seconds in the audit host;
- 992 PICA service calls: 68 register writes, 819 GSP commands, 104 framebuffer
  updates and one force-black update;
- 120 ordered service flushes;
- 409 native DSP submissions carrying 65,440 stereo PCM frames;
- 120 abstract input service reads consumed by native 3DS HID;
- 63 filesystem reads carrying 19,269,907 bytes through six logical opens;
- 74,835,359-byte process/CTR/DSP/PICA state round trip;
- checked read lease at the real `0x00100000` process entrypoint.

These counts validate lifecycle and service separation, not rendered-image
parity: the smoke backend acknowledges PICA work but does not draw.

## NRI/Vulkan graphical vertical

`triaevum_oot3d_module_host` is the first title-neutral graphical consumer of
the private TAM. It links the public module loader, application host and PICA
renderer adapter, but does not link `oot3d_game_module` or another private
title library. The executable composes this path:

```text
game.tam -> TriAevum v1 ABI -> typed PICA/audio/filesystem/input services -> NRI/Vulkan + PCM/HID + confined content/save I/O
```

The renderer retains completed CTR display transfers because framebuffer
selection and transfer production are separate operations. A later scanout
update can therefore present a buffer produced by an earlier service batch,
matching the native double-buffer lifecycle instead of requiring an address
match in the newest batch.

Example bounded validation:

```text
set OOT3D_VULKAN_VALIDATION=1
triaevum_oot3d_module_host --module <game.tam> --cache <module-cache> --content <content.tap> --resource-root <runtime-resources> --renderer nri --width 1280 --height 720 --frames 180 --max-seconds 30 --screenshot <frame.bmp>
```

Evidence recorded on 2026-09-02 for the supported EUR recipe:

- 180 real module frames completed through NRI/Vulkan validation;
- 83 ordered PICA batches, 1,608 draws, 83 display transfers and 233
  completion publications;
- zero Vulkan validation errors after removing competing render-pass
  ownership from the host;
- direct framebuffer readback contains the native night scene, including sky,
  moon, terrain and vegetation;
- 613 native DSP HLE submissions crossed the audio ABI, carrying 98,080 stereo
  frames (392,320 PCM bytes) at the native 32,728 Hz rate into the initialized
  host audio queue;
- PE dependency audit confirms that the public host has no dependency on
  `oot3d_game_module.dll`.
- a bounded 120-frame host run produced 120 physical input polls, 120 module
  reads and 63 filesystem reads carrying 19,269,907 bytes while preserving the
  expected framebuffer and audio counts;
- explicit session shutdown closes module-owned filesystem handles before the
  registered service contexts are destroyed; the bounded host exits cleanly.

The DSP mixer and title memory stay inside the private module. The public host
receives only validated interleaved PCM payloads, stream state and queue counts;
no title pointer or DSP shared-memory address crosses the ABI.

Keyboard, mouse, controller buttons/sticks, touch, gyroscope and accelerometer
are normalized by an isolated host adapter. The private module sees only the
single abstract 3DS control surface and projects it to native HID at each guest
refresh; it has no dependency on SDL or desktop key codes.

The host parses `content.tap`, validates every referenced file size and exposes
an immutable logical content table plus a canonical save root. The generic
filesystem adapter rejects absolute paths, traversal, drive prefixes and
content writes. The private module validates the manifest, code and ExHeader
through this service, mounts `code.bin` from bytes and serves native CTR RomFS
and save sessions through the same boundary. Existing path-backed CTR tests
remain as the linked-oracle route only.

## Completed product vertical

The framebuffer, PCM, filesystem, input and state contracts are covered by the
module tests and strict real-content smoke. Forge now uses the direct
structural-IR-to-object backend, and the graphical host has completed the
isolated dump-to-open-title package test. Ongoing scenario expansion remains a
regression-coverage activity rather than a missing release boundary.
