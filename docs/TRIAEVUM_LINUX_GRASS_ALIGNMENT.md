# Linux Runtime Alignment, September 11

Follow-up: the [post-1c audit](TRIAEVUM_POST_1C_REGRESSION_AUDIT.md) found that
the preserved SDK title still lacked the VFP64 correction and updated it.
The renderer/Grass results below do not establish title-module parity.

Runtime source: `d3ffc1f` on `port/linux-nri`, including the accepted adaptive
Grass preset and current TopScreen/HUD corrections. This update qualifies the
installed private Linux runtime, not a new public release or actual Steam Deck.

## Build and Deployment

- Woke the physical Linux test host with Wake-on-LAN; authenticated SSH.
- Compared the source mirror with the previous `94cb1c1` archive and current
  export. Reviewed three newer HUD files before replacing them. Backed up
  changed files; preserved unrelated files and identical-file timestamps.
- Incrementally built `triaevum_public_runtime` and `oot3d_game_module` with
  three jobs, first on the native host, then in the existing Steam Runtime 4
  SDK. No title AOT generation or title-module recompilation.
- Rebuilt the baseline PICA pack: 200 SPIR-V modules. This is not exhaustive
  shader coverage; source misses still use the persistent shader cache.
- Reassembled and installed the private Flatpak with the SDK runtime, neutral
  module and shader pack. The menu entry now selects the new runtime, not
  merely an updated developer build directory.
- Applied only `Graphics.Grass` and `Graphics.GrassSavedPreset` to the three
  existing test/user installations, preserving other settings and backing up
  their configurations. ROM data and game saves were not replaced.

Installed runtime SHA-256:
`61edd977aa1be7b38c7fab2bd49c6c27c3bfe7c48bc336d3ac2dab9206445b3d`.
Flatpak commit:
`86f14c30c775d3a591a37adc7b7763448f94ab5997016fa1897d11294a477891`.

## Installed Launcher Check

Direct runtime probes succeeded, but the first normal menu-entry check stayed
in Forge: the existing activation receipt still hashed the previous runtime.
The bounded probe terminated it, rather than leaving the window/process open.

Explicitly migrated the private receipt after checking the installed binary
against the package catalog and checking the unchanged title plugin. Updated
the launch-profile hash and routed the packaged shader corpus and same-device
cache. Identity checks were not removed. The second normal `flatpak run`
launch passed through Forge into the game, produced framebuffer captures at
900 and 1500, and exited normally after 35 seconds. Temporary run limits and
capture arguments were removed and the permanent profile hash restored.

This manual, verified test-installation migration is **not** an implementation
of automatic receipt migration for future public Flatpak updates. The frozen
Forge in this private Flatpak was retained; portable Forge rebuilding and the
full new-release audit remain separate work.

## Verification

Physical RTX 4060, native Vulkan/NRI, KDE Wayland, 1280x720. No X11 requirement
for these runs; no desktop screenshot capture.

| Probe | Result |
| --- | --- |
| Fixed native intro, 1,320 frames | Exit 0; seven wide-view framebuffer captures. Grass present, including distant coverage. |
| Native cadence, 1,320 frames | Exit 0; 110 captures spaced by 11 frames, sampling both frame parities. No near-black captures. |
| Interpolated 2x, 1,800 presentations | Exit 0; interpolation active in telemetry; 153 captures. Three near-black samples during startup/loading-to-intro fade; none after that phase. |
| Uncapped native throughput | Exit 0; 180 warmup frames excluded, 1,140 measured, 16.686953 s: **68.32 native frames/s**, 14.64 ms/frame. |
| Normal installed Forge/menu launch | Exit 0; game report and two framebuffer captures. |

Throughput uses fixed 1/30 steps, no interpolation, VSync, host pacing, SDL
limiter or captures. No build ran during it. This is a single Linux throughput
measurement, **not** a Linux historical speedup percentage or 60-FPS minimum.
The prior Windows -33% acceptance is documented separately. Dense readbacks
stall rendering: their run times are not performance results.

At native frame 840, telemetry reports 163,731 visible blades, 49 draw calls,
GPU compaction active, no static upload, and maximum actual cluster size 3,558.
This matches the accepted Windows visible-root count, without claiming pixel
identity between drivers. Raster inspection confirms populated terrain/title.

- Linux cluster, TopScreen profile and native UI lifecycle test programs pass.
- 19 Python tests pass: Grass comparison, coverage, product defaults including
  the built executable, and probe isolation/argument validation.
- Product-default tests now assert the accepted adaptive preset and normalize
  expected floats to the C++ float32 representation. The old assertions still
  expected the retired tuft configuration. Five product tests also pass on
  Windows against its built runtime.

## Evidence and Limits

Private Linux evidence: `~/.var/app/io.github.coccofresco.TriAevum/data/probes/`
under `aligned-d3ffc1f-*`; backups and alignment inventory under
`~/triaevum-align-d3ffc1f-backup/`. Private installer:
`~/triaevum-flatpak-proof/TriAevum-Linux-d3ffc1f.flatpak`.
No game data, framebuffer captures or driver caches are committed.

SSSR remains disabled in the Linux build as previously documented. No Android,
physical Steam Deck, exhaustive gameplay or validation-layer qualification is
claimed. Sampling rules out the old sustained alternating-black failure in
the tested intro, not every possible transient on every device.
