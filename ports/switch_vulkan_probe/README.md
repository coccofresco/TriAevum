# Switch Vulkan WSI probe

This is an isolated homebrew feasibility probe. It does not enable Vulkan in
the OoT3D product and it does not exercise the PICA renderer. It checks the
small platform seam that an eventual port needs: a statically linked Vulkan
ICD, `NWindow`/`VK_NN_vi_surface`, an uncapped-choice swapchain and concurrent
acquire/submit/present.

The Vulkan/VI probe itself is a small project-owned implementation written
against the public Vulkan and libnx interfaces; it does not copy Citra/Azahar
renderer code. Its architecture was informed by the platform seams in Tico
Azahar and by NXVK's documented linking contract. The separately identified
`nxvk_newlib_compat.cpp` is adapted from NXVK's GPL-2.0-or-later
`switch/smoke/nvk_compat.c` and preserves its copyright and SPDX notice. The
complete licence text is included in [`COPYING`](COPYING).

## Dependency and licence boundary

The build requires an explicit NXVK path and never downloads, installs or
silently selects a driver. The currently audited NXVK Switch fork identifies
its new fork files as GPL-2.0-or-later and is statically linked into an NRO.
Treat a resulting binary as a GPL distribution decision. This probe is kept
separate so that building it cannot accidentally change the product's licence
or dependency closure.

The compatibility adapter is only a link/feasibility boundary. Horizon newlib
still has no regex implementation or POSIX file locking here, so `regcomp()`
and `flock()` fail explicitly. A successful probe must confirm that NXVK/Mesa
handles those failures without silently losing a required configuration or
cache path. Until that is observed on hardware, the adapter and driver are not
qualified for the product.

Accepted dependency layouts are:

- an installed/staged prefix with `include/vulkan`, `lib/libnvk.a` and
  `lib/libnvk_support.a`; or
- an NXVK source tree whose `make` packaging step produced
  `switch/build/pkg/lib/libnvk.a` and `libnvk_support.a`.

The devkitPro prefix must also provide the Switch `zlib` and `expat` static
libraries used by the driver support archive.

## Donor choice

Dekopon at `2cfda73c5` (2026-08-27) is now a useful reference for a native
Deko3D present path; its preceding `uam` work also demonstrates runtime
GLSL-to-DKSH compilation. It is not yet the shortest renderer donor for this
project: its Deko3D rasterizer is explicitly a placeholder, cross-format
reinterpretation and custom-texture upload remain unimplemented, and enabling
it deliberately supersedes the existing Vulkan/OpenGL backends.

NXVK therefore remains the shortest feasibility route for reusing OoT3D's
qualified Vulkan renderer rather than translating that renderer to Deko3D.
Dekopon's present, frame-context and `uam` structure should remain a reference
or fallback if the Vulkan/NRI feature audit exposes a hard NVK limitation.

Build with:

```sh
scripts/switch/build-vulkan-probe.sh /absolute/path/to/nxvk
```

The build script validates both AArch64 ELF output and the NRO header.

## Hardware result

The probe presents an orientation pattern with these fixed corners:

- top left: red;
- top right: green;
- bottom left: blue;
- bottom right: yellow.

A cyan top-left-to-bottom-right diagonal and a three-position frame marker make
vertical inversion, stale images and frame overlap visible. The probe requests
three swapchain images (clamped to the surface capabilities) and keeps three
submissions in flight.

Present mode is selected in this order: `IMMEDIATE`, `MAILBOX`,
`FIFO_RELAXED`, then the mandatory `FIFO` fallback. The probe adds no sleep,
frame cap or application-side VSync. A FIFO-only driver/display can still
impose presentation pacing; that fact is recorded rather than hidden.

Press `+` to exit. Sparse receipts are written to:

```text
sdmc:/switch/oot3dre/switch-vulkan-probe.txt
```

The receipt records device, extent, requested/actual image count, selected
present mode, frame throughput and average host time spent in acquire, submit
and present. It is written at startup, after the first 300 presentations,
occasionally thereafter, and on clean exit; there is no per-frame SD I/O.
