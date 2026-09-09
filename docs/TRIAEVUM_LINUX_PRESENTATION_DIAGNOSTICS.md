# Linux Input and Presentation Qualification

Current Linux default: native Wayland with shared graphics/present queue and
inline presentation; X11 fallback remains available. The user confirmed the
shared-queue Wayland probe no longer flashed. The history below distinguishes
that confirmation from validation, framebuffer probes and earlier mitigations.

## Escape (2026-09-09)

Escape belongs to the host window, not to the guest or the application-exit
condition. `fast/MouseCapturePolicy.h` preserves the released state across
polls; a gameplay click can recapture, but that click must not fire a guest
action. F1/native touch ownership prevents recapture. Both input consumers
exclude Escape from guest bindings and no longer use it to terminate execution.

Tests: `mouse_capture_policy_tests` passes with Windows Clang 22 and Linux
Clang 19. A native Linux X11 window accepted three XTest Escape presses and
remained present, rendering subsequent frames until the diagnostic run's time
budget expired. Wayland/manual recapture and the rebuilt Windows application
still need interactive qualification; a unit test is not that qualification.

## Two Separate Synchronization Defects

1. The swapchain clear/overlay render passes did not synchronize previous
   late depth writes before discarding/reusing their depth attachment. Vulkan
   synchronization validation reported three WRITE_AFTER_WRITE hazards in a
   65-second run. Include early/late fragment tests and depth writes in the
   incoming dependency. Apply the same depth-reuse rule to the Shadow2D pass.
2. NRI scanout discards the previous image contents, yielding an UNDEFINED to
   COLOR transition with source stage NONE. The host acquisition semaphore
   waits at COLOR_ATTACHMENT_OUTPUT. That is not the required dependency chain
   protecting the layout transition while the presentation engine still reads
   the image. `NriTextureTransitionDesc::ExternalWaitStages` lets the owning
   pass declare the external wait scope; scanout supplies COLOR_ATTACHMENT.
   Other textures retain their existing transition scopes.

The second rule is specified in the Vulkan documentation's
[swapchain-acquisition synchronization example](https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html).
Do not replace it with device-wide waits, dropped frames, or changes to native
materials/depth tests. It is a general presentation contract, not a scene fix.

NRI's stage constants are not ordinary empty/all bitmasks: `ALL` is zero and
`NONE` is `0x7fffffff`. The first acquisition patch incorrectly ORed `NONE`
into existing scopes, creating additional hazards. `MergeNriStageScopes` now
treats the sentinels explicitly; `nri_stage_scope_tests` passes on Linux Clang
19. The corrected 75-second validation run (`acquire-scopes-fixed`) reports
zero Vulkan validation errors. The erroneous merge is not retained.

**Intermediate status, before the queue-policy fix below:** the user confirms that interpolated presentation no longer flashes,
but native Wayland at 30 FPS still alternates visible/black frames, including
a fresh launch without readback (`native30-visible-no-probe`). Switching ONLY
the SDL video backend to X11, at 30 FPS without readback or validation, produced
a 120-second run the user reports as stable (`native30-x11-no-probe`).

Commit `170382f` selected `x11,wayland` before SDL
video initialization for Linux Vulkan. Explicit `SDL_VIDEODRIVER`/SDL hints
remain authoritative. It changes the window-system backend, NOT Vulkan,
gameplay timing, interpolation or effects. Windows, Android and non-Vulkan
backends are untouched. SDL2 supports ordered driver lists in its
[video initialization implementation](https://github.com/libsdl-org/SDL/blob/SDL2/src/video/SDL_video.c).
The chosen driver is logged on startup. XWayland must remain available in the
Flatpak permissions for this default; Steam Deck hardware is not yet qualified.

This is a qualified presentation-route mitigation, NOT proof that the native
Wayland defect is repaired or that its cause is the driver. Neither clean
validation nor framebuffer statistics prove visible presentation. The earlier
correlation with Link/Epona has not been proven to originate in actor assets or
animation; no actor-specific fix exists.

The automatic-default build then completed a 100-second, 30-FPS run with the
actual user preset loaded, without readback/validation (`qualified-policy-default30`):
2,510 presentations, 193,731 native draws, 139,729 toon draws, 2,412 grass
provider executions, zero reported effect-graph dispatch failures. These are
execution counters, not a performance benchmark or a substitute for visual
confirmation. Its X11 window was verified without `SDL_VIDEODRIVER` override.
The driver-policy test passes under the shipping Steam Runtime SDK. Forge's
ordinary ROM import refreshed the installed runtime receipt in 4.4 seconds.

**Status at `170382f`:** falling back to Wayland when X11 is unavailable
preserves launch capability but does NOT guarantee flicker-free output. X11 is
a temporary qualified route, not a new universal system requirement. A complete
Linux release still needs uninstrumented native-Wayland qualification at both
30 FPS and interpolated rates. Do not force interpolation or silently disable
effects as a workaround.

## Native Wayland Queue Policy, September 9

`wayland-protocol30` (60 seconds, native 30, FIFO, no readback) still flashed
according to the user. Its client protocol trace shows the Vulkan WSI attaching
alternating buffers with explicit DRM syncobj acquire/release points. SDL's
default queue requests damage/frame callbacks but does not repeatedly attach or
commit a second image. This rules out that particular double-present hypothesis;
it does not establish that the driver, compositor or renderer is responsible.
The installed SDL2 library is SDL2-compat, backed by SDL3. The display reports
VRR/HDR incapable, 2560x1440 at approximately 59.95 Hz.

An opt-in `TRIAEVUM_VULKAN_PRESENT_DISPATCH` diagnostic now separates host
dispatch from GPU queue choice without changing shaders, assets or frame rate:

- Unset/`auto`: same graphics/present queue on Wayland when supported; the
  existing worker policy on other video drivers.
- `async`: diagnostic override reproducing the earlier separate-queue worker.
- `inline`: same GPU queue topology, but call present on the main thread.
- `graphics`: use graphics queue 0 for presentation if the family supports it,
  with inline dispatch. Devices requiring a separate present family retain it.

Invalid values are rejected. No per-frame queue-idle wait is introduced. These
are startup diagnostics, not new persisted user settings. Fast standalone policy
tests pass with Windows Clang 22 and the Linux shipping SDK's Clang 19.

`wayland-inline30`: 2,701 frames over a bounded 90-second run, native profile,
640x360, FIFO, no readback/validation. **User observed fewer but still present
flashes**. Removing the worker alone is therefore not a fix.

`wayland-graphics30`: completed a bounded 100-second run, 3,084 presentations.
The effective profile changed from Authentic to Custom and the output changed
from 640x360 to 1280x960 during the run; it is not a single-setting benchmark.
**User confirmed no flashes in this run.** Unlike inline dispatch on a separate
queue, the shared-queue path passed this visual check without any framebuffer
readback. This identifies queue selection as an effective correction on the
tested stack; it does not by itself prove a Vulkan specification violation or
assign blame to NVIDIA/KWin.

The backend now supplies SDL's actual video driver to the isolated queue policy.
Wayland defaults to graphics queue 0 for present, called inline. No global idle,
new fence wait, dropped frame or forced interpolation is added. A mandatory
separate present family is preserved (not yet qualified on such hardware).
Windows, X11 and other surface policies are unchanged. SDL now prefers
`wayland,x11`, retaining explicit user hints and X11-only desktop fallback.
The unit tests cover both defaults, diagnostic overrides and single/separate
family devices. This supersedes the temporary X11-first mitigation above.

Steam Deck/AMD hardware qualification remains outstanding: NVIDIA desktop
success is not a substitute. The dispatch diagnostics can reproduce the old
policy without new builds, and must not be confused with graphics presets.

Default-policy follow-up (no forced SDL video driver or dispatch override):

- `wayland-ordered-default30-validation`: 65 seconds, 1,951 frames, zero Vulkan
  validation errors, no readback. The output/profile changed near the end of
  the interactive run; this is not a performance comparison. Diagnostics show
  one requested graphics queue, present family 0/index 0.
- `wayland-ordered-full-x2`: 100 seconds, configured x2 with the full user preset,
  no readback/validation. The bounded diagnostic history retained 4,000 frames,
  227,567 toon draws and 3,796 grass executions, with zero effect-graph dispatch
  failures. Do not divide the capped 4,000 records by run time to infer FPS.
  This confirms the effects path remains active, not independent visual proof.
- Installed private Flatpak commit:
  `7401b203749cf5efe8c2ed1ca6d9723667ad658bbb70d33b4ff7f0a17e1a2972`.
  Runtime SHA-256:
  `8a370a9c5aa35bc3ccf1acd3f1a71c449f86cdb00023771d0ced0fba474a046b`.
  The desktop `TriAevum-Linux-test.flatpak` was re-exported. Ordinary Forge
  import refreshed the installed receipt in 4.5 seconds, preserving user data.

## Nonblocking Framebuffer Probe

Set `TRIAEVUM_VULKAN_SCANOUT_PROBE` to a writable CSV path before launching.
`fast/backends/vulkan_scanout_probe.*` records the final swapchain image after
composition, before presentation. It copies into a per-in-flight-slot coherent
buffer and reads only after the host's existing slot fence. It adds no CPU/GPU
wait or queue-idle call. When unset it allocates nothing and records no commands.

Each row contains frame/image index, dimensions, native-scanout status, sampled
black pixels, mean RGB and pixel hash. Sampling every eight pixels is a cheap
whole-image check, not proof of every pixel. Memory is bounded to two current
image buffers. The probe still adds transfer work/barriers; compare against an
uninstrumented run. Do not reinterpret all boot/fade frames as defects.

Before the acquisition correction:

- Wayland, effects off, native 30: 1,951 frames, no isolated whole-image black
  drops after the loading fade.
- X11, same native-30 control: 2,249 frames, same result.
- Wayland, effects off, actual interpolated x2: 3,895 frames, same result.

After the acquisition correction, probes named `actors-default-x2` (4,070
frames) and `actors-default-native30` (1,801 frames) likewise found no black
frames after boot or isolated mean-RGB drops. Their names are misleading:
the frame-rate environment override suppresses loading the graphics config,
so BOTH were effects-off controls, NOT tests of the user's default effects.
It also leaves factory VSync/output dimensions, ignoring the helper's JSON.

Important: Authentic forces native 30. Conversely, any graphics environment
override bypasses persisted graphics configuration. The helper now selects
Custom in its isolated config for effects-on tests and sets the frame rate
there, without graphics environment overrides. Never infer effective settings
from filenames or requested JSON alone. The legacy diagnostic root fields
`render_mode=authentic` and `effects_enabled=false` are hardcoded; consult the
per-frame fidelity profile, pass/draw counts and presentation state instead.

The probe adds GPU work/barriers and may suppress the visible race: the user
confirms black flashing without it at 30 FPS even though readback controls are
clean. Continuous synchronous screenshots are even more intrusive and are
unsuitable for this race or for performance measurements.

## Unresolved Stalls

With the unchanged default grass preset, the cold title scene builds 4,661,429
anchors. One source costs approximately 12.4 seconds in extraction plus 1.45
seconds in clustering; the first frame waits approximately 15 seconds in total.
This is independent of the presentation defect. A stronger hash alone failed
to improve the representative benchmark and was discarded. No density/quality
reduction or delayed missing-grass workaround has been shipped as a fix.

## Developer Reproduction

- Authoritative checkout: `I:/TriAevum-public/source-repository-clean`.
- Linux source mirror: `/home/xander/triaevum-linux`; SDK/build paths and
  commands: [Steam Runtime build](TRIAEVUM_STEAM_RUNTIME_BUILD.md).
- Private helpers: `I:/oot3dre_work/linux-port-proof/` and
  `/home/xander/triaevum-flatpak-proof/`: `run-flatpak-frame-probe.py`,
  `analyze-scanout.py`, `x11-escape-smoke.py`, `refresh-proof-runtime.py`.
- Private evidence: `~/.var/app/io.github.coccofresco.TriAevum/data/probes/`.
- The probe helper creates independent configs and saves, uses bounded runs,
  supports explicit `--video-driver wayland|x11`, `--scanout`, `--validation`,
  and `--no-capture`. GPU validation is private tooling, not a release payload.
- After updating an installed private Flatpak, refresh Forge's runtime receipt
  through its ordinary import workflow. Do not leave a launcher targeting an
  old binary or a mismatched receipt. Re-export the desktop bundle only after
  the runtime is finalized. Preserve the user's configs and savedata.

These remain private test builds, not a public Linux/Steam Deck qualification.
No ROM, derived cache, SDK or capture belongs in the public source/package.
