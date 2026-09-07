# Switch Citra/Azahar donor audit

## Decision

The existing Switch Citra/Azahar ports are useful donors, but only at the
Horizon platform boundary.  The fastest credible Vulkan route is:

```text
OOT3D whole-AOT gameplay
    -> existing typed PICA renderer and Linux Vulkan implementation
    -> thin libnx/NXVK platform adapter
    -> NWindow / VK_NN_vi_surface / VI swapchain
```

It is not a transplant of Citra's emulation core or PICA renderer.  Dynarmic,
3DS services, timing, shader JIT and the frontend remain out of the product.
The source-native OOT3D tree also remains documentation only; gameplay is the
same 12,419-function ARM64 whole-AOT graph.

The current SDL2/Mesa/EGL/OpenGL product remains the buildable baseline while
the Vulkan route is proven in an isolated homebrew probe.  No NXVK or Azahar
source has been copied into the product by this audit.

## Audited implementations

### Dekopon

[Dekopon](https://github.com/PalindromicBreadLoaf/dekopon) is the most complete
current reference for the Switch platform layer.  Its current tree demonstrates
a libnx application using NXVK, `NWindow`,
`VK_NN_vi_surface`, native input/touch/motion, `audout`, application lifecycle,
thread affinity and on-disk shader/pipeline caches.

Useful reference units are its Switch
[bootstrap](https://github.com/PalindromicBreadLoaf/dekopon/blob/master/src/citra_switch/citra_switch.cpp),
[window](https://github.com/PalindromicBreadLoaf/dekopon/blob/master/src/citra_switch/emu_window.cpp),
[input](https://github.com/PalindromicBreadLoaf/dekopon/blob/master/src/citra_switch/input.cpp),
[Vulkan scheduler](https://github.com/PalindromicBreadLoaf/dekopon/blob/master/src/video_core/renderer_vulkan/vk_scheduler.cpp)
and
[present path](https://github.com/PalindromicBreadLoaf/dekopon/blob/master/src/video_core/renderer_vulkan/vk_present_window.cpp).
The reusable result is the platform contract, not its emulator architecture.

Patterns worth adapting after a probe succeeds:

- an application-type NRO, explicit heap and applet lifecycle;
- direct NXVK ICD loading and VI surface creation;
- a bounded GPU queue, explicit fences and a small number of frames in flight;
- Switch-aware worker count, affinity and priority;
- page-aligned audio buffers on a dedicated thread;
- shader/pipeline caches and bounded logging;
- fast-load CPU boost only during loading, restored before gameplay.

Its fully asynchronous pipeline mode is not an acceptable correctness default:
it may skip draws while a pipeline is unavailable.  Missing effects were one
of the observed failures of the current port, so pipeline creation stays
synchronous until a warmed cache and complete frames are proven.

Dekopon head `2cfda73c5` (2026-08-27) also adds a native
[Deko3D presentation path](https://github.com/PalindromicBreadLoaf/dekopon/commit/2cfda73c5a916a1ddedfba14788fbd38d965659d),
after `5c26cb354` added runtime GLSL-to-DKSH compilation through `uam`.  Those
commits make its presentation, frame-context and shader-compiler seams useful
donors even if NXVK is selected.  They do not make its entire renderer ready:
the Deko3D rasterizer is still marked incomplete, cross-format texture
reinterpretation and custom-texture upload remain unimplemented, and enabling
the backend intentionally replaces Vulkan/OpenGL.  Reusing the existing OOT3D
Vulkan renderer through NXVK therefore remains the shorter route; Deko3D is a
credible fallback if the Vulkan feature gate finds a hard driver limitation.

### Tico Azahar

[Tico Azahar's `switch` branch](https://github.com/ticohq/tico-azahar/tree/switch)
is an independent cross-check of the same design.  At audit time the branch
head was `d3f816fb4f8b02fa0a62208e88fc4e71c7952788`.  It forces Vulkan on
Horizon and supplies libnx implementations for
[HID/motion](https://github.com/ticohq/tico-azahar/blob/switch/src/input_common/switch_hid.cpp),
[audio](https://github.com/ticohq/tico-azahar/blob/switch/src/audio_core/libnx_sink.cpp)
and the
[Vulkan platform boundary](https://github.com/ticohq/tico-azahar/blob/switch/src/video_core/renderer_vulkan/vk_platform.cpp).
It is a useful second implementation for checking WSI, synchronization,
driver-cache and thread decisions; it is not the renderer to import.

### Older Citra/libretro and Deko3D experiments

The old [citra-libretro Switch build](https://github.com/zorn-v/citra-libretro/blob/master/Makefile)
depends on the OpenGL context supplied by RetroArch. The locally recovered
2020 package contains only an NRO, not auditable source. These are historical
proof that Mesa/NXGL can run Citra, not a maintainable donor.

The older Deko3D path in
[azahar-nx](https://github.com/devgsantos/azahar-nx) still lacks substantial
PICA texture, generated-shader, format, geometry/proctex and pacing work.  The
newer Dekopon work supersedes it as the Deko3D reference, but is still not the
shortest route to a correct OOT3D renderer.  Deko3D remains a possible later
native backend, not the current implementation target.

## NXVK gate

The candidate driver is the
[NXVK Switch fork](https://github.com/PalindromicBreadLoaf/nxvk/tree/switch).
It exposes Vulkan and VI WSI directly and can also expose OpenGL through Zink.
The driver is experimental and must be qualified against the Vulkan features
actually requested by the existing OOT3D renderer; the fact that Azahar boots
does not prove this separate feature set.

Before changing the product backend, build a standalone NRO that:

1. loads the NXVK ICD without `dlopen`;
2. creates an `NWindow`, VI surface and three-image swapchain;
3. renders an oriented colour/test-pattern frame without an implicit limiter;
4. records acquire, submit, GPU completion and present timing separately;
5. survives suspend/resume and handheld/docked transitions;
6. reports device limits and every required Vulkan feature/extension.

The probe may additionally run the existing OpenGL renderer through Zink as a
diagnostic A/B test.  That can isolate faults in the current EGL/OpenGL present
stack, but it is not assumed to be faster.

## Licensing boundary

Azahar, Tico and Dekopon are GPL-2.0-or-later.  The NXVK fork documents its new
Switch files under GPL-2.0-or-later and is statically linked on Horizon; its
[README](https://github.com/PalindromicBreadLoaf/nxvk/blob/switch/switch/README.md)
therefore treats the application as a combined GPL-covered work.  This
repository already contains an attributed GPL Azahar shader-decompiler subset,
but a distributable complete product still needs an explicit top-level licence
and corresponding-source policy before NXVK is linked into release artifacts.

Until that policy is fixed, donor code is used as a behavioural reference and
the feasibility probe stays isolated.  Reimplementing small libnx adapters
locally avoids unnecessary emulator coupling, but it does not remove the
licence obligations of a statically linked NXVK driver.

## Immediate priorities

The two tracks are intentionally independent:

1. Keep measuring the Linux OpenGL proxy and optimize shared CPU/whole-AOT and
   GL hot paths. Persistent identity-backed geometry buffers and persistent
   RomFS object streams are now implemented. The remaining relevant donor
   patterns are persistent/ring storage for uniforms and dynamic uploads,
   byte-bounded cache lifetime and a capability-gated program-binary cache.
2. Qualify NXVK in the standalone probe.  If it passes, adapt the existing
   Linux Vulkan renderer to Horizon and reuse Dekopon/Tico only for WSI,
   lifecycle, input, audio, cache and scheduling.

Eden remains a boot, contract and visual smoke test.  NXVK performance,
affinity, clock behaviour and physical presentation are accepted only from
telemetry on real Switch hardware.
