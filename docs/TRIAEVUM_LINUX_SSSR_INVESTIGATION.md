# Linux Performance and Windows SSSR Investigation

Date: 2026-09-08. Branch: `port/linux-nri`.

## Verified, Not Assumed

- Linux RTX 4060, native Vulkan/NRI, current default effects and 2x presentation.
  In a 45-second instrumented intro run, 1,499 GPU samples after frame 120:
  mean 3.808 ms, p95 4.714 ms, maximum 5.773 ms. Grass mean 1.134 ms;
  native PICA mean 1.747 ms. These are GPU costs, NOT native gameplay FPS.
- CPU frame 55 stalls for 14.323 seconds, with 14.261 seconds inside Grass.
  Separate build instrumentation confirms the largest source spends 13.218
  seconds extracting 4,189,022 anchors, 646 ms clustering, 197 ms transforming.
  Total scene placement contains 4,661,429 anchors. The synchronous admission
  in `GrassStaticPlacementCache::Resolve` waits for all requested placements.
- Steady Grass selection is also measurable: roughly 4-8 ms in sampled logs,
  despite drawing only about 75,000 visible blades. Do not confuse this CPU
  cost with GPU Grass cost or the cold placement build.
- Periodic framebuffer readback introduces approximately 0.4-second stalls.
  Interactive Linux launch no longer takes periodic screenshots by default.
  Use `TRIAEVUM_CAPTURE_FRAMES=1` explicitly for image evidence, never as a
  steady-state performance measurement.
- Windows RTX 3060, release executable with isolated configuration:
  `FidelityFXSSSR` dispatched 1,905 times, zero provider fallback, zero profiled
  or calibrated material draws in the unmodified intro material configuration.
  This disproves an unconditional Windows provider failure, not a visual bug.
- A diagnostic explicit material selected from the observed texture inventory
  produced 925 SSSR dispatches and 4,368 profiled/calibrated material draws.
  Material debug framebuffer shows the selected surface mask. This proves
  routing/guide coverage, NOT correct reflection radiance or final appearance.
- Linux build still excludes FidelityFX SSSR; do not label Hi-Z fallback as SSSR.

## Reproducible Tools

`tools/triaevum_release/probe_renderer.py INSTALLATION EXECUTABLE OUTPUT`
creates an exclusive private output directory, copies configurations, isolates
save/output paths, bounds execution and captures GPU diagnostics plus framebuffer.
Options include `--reflections FidelityFXSSSR`, `--debug-view 1`,
`--material-hash HEX`, `--seconds 45`, and `--extended-diagnostics`.
Material overrides are diagnostic fixtures ONLY, never shipped title fixes.
Use observed `reflection_texture_usages.texture_hash`, not a guessed or
replacement-texture RGBA hash. An unmatched rule proves nothing.

Private evidence root on the development machine:
`I:/oot3dre_work/linux-port-proof/` (not distributed).
Windows useful runs: `windows-sssr-enabled`, `windows-sssr-profiled`.
`windows-sssr-probe` used an invalid enum spelling and must NOT count as SSSR
evidence; `windows-sssr-material` used an unmatched texture hash and must NOT
count as explicit-material coverage. Keep these failures documented.
Linux GPU evidence: `gpu-default.json`; later Grass build timing is logged by
`OOT3D_GRASS_DIAGNOSTICS=1` as `[grass-build]` in the remote launch log.

## Next Corrections, In Order

1. Black flashes remain unresolved. Determine whether present output or native
   scene content goes black using a short consecutive framebuffer sequence and
   user confirmation in a capture-free run. Do not assume a swapchain bug:
   Linux uses the raw Vulkan swapchain fallback, Windows NRI-owned swapchain,
   but this difference alone is not causal evidence.
2. Remove cold Grass extraction from the critical presentation path without
   losing first-visible-frame coverage or changing presets. Investigate bounded
   view-prioritized admission and reusable placement storage; preserve stable
   identities, masks, spacing and camera-cut coverage. The measured bottleneck
   is extraction, not shader compilation or clustering.
3. Compare reflection Off/SSSR with matching native frames and a verified
   reflective material; inspect hit confidence, depth, normals and resolved
   radiance. Unprofiled material eligibility requires native specular semantics;
   transparent/depth-write-disabled draws are currently excluded. Do not claim
   water support merely because the editor offers a Water profile.
4. Port the FidelityFX build dependency before testing actual Linux SSSR.
5. Benchmark separately: cold start, warmed native updates, 2x presentation;
   disable VSync and caps for throughput and omit captures/log-heavy diagnostics.

No fix for black flashing, complete SSSR visual correctness, or a steady-state
FPS increase is claimed by this investigation.
