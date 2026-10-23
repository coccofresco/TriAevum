# Surface lighting ownership and brightness preservation

## Contract

Per-pixel lighting replaces surface illumination, not the authored appearance of
sky, backgrounds, atmosphere or UI. Native, NativeMemory and Authentic remain
canonical. F2 retains its existing native-presentation mask and suspends the
authored environment; it does not force configured Per-pixel modes to Native.
Native gameplay/environment updates continue: disabling native light
contributions is a presentation decision, not a guest-state mutation.

The implementation separates:

- native appearance and native alpha/coverage;
- material reflectance and baked vertex/texture tint;
- a scene-brightness reference independent of enabled native direct sources;
- effective direct illumination, shadows and ambient occlusion.

Classification comes from typed native composition ownership. It must not use
texture names, material hashes, screen position, draw distance or scene-specific
corrections. Distant world geometry remains a surface, not a background exemption.
Unknown ownership must not be silently promoted to a relightable surface.

## Implementation sequence

1. Carry composition-layer ownership into shader admission. Preserve atmospheric
   appearance instead of applying the world surface-lighting replacement. Verify
   shared-program cache transitions between world and atmosphere.
2. Add an explicit brightness-reference policy to the scene Environment owner.
   Native source gains may all be zero without discarding the reference. Resolve
   the low-frequency lighting budget on the CPU, not with framebuffer readback or
   camera-dependent automatic exposure. Version and document persistence.
3. Use the same resolved lighting inputs for Per-pixel, PBR and Grass. Keep
   reflectance application single-owned, specialize inactive source work, and
   avoid repeated preparation/upload of unchanged illumination.
4. Qualify with deterministic scene captures, native regression captures,
   production GPU lighting probes, real F1 widgets and structural diagnostics.

Each implementation increment records its verification here and is committed
separately. Pre-existing worktree changes are excluded from these commits.

## Brightness interpretation

Preservation means maintaining the environment's reference light level and
palette while allowing new light directions and shadows. It cannot promise
pixel-identical world shading after replacing those directions. Sky/background
appearance has a stricter invariant: changing surface-light settings must not
change it at the same atmosphere state and presentation settings.

Do not multiply already-lit native RGB by another complete lighting result, add
an arbitrary minimum brightness, bake modified textures, or use global exposure
to conceal a missing sky contribution. Authored dark channels remain dark.

## Acceptance and evidence

- Original direct-light gains zero: sky/background retain their authored color.
- Brightness reference remains usable without evaluating original direct lights.
- Key/fill, dynamic point groups and material controls remain independent.
- Atmosphere, alpha, depth/stencil, native draw order and UI remain protected.
- Native and native-memory outputs remain unchanged. F2 retains its existing
  suppression policy; its sky correction is qualified separately below.
- Room/time/environment transitions update the reference without camera pumping.
- Performance claims require steady-state measurements, not capture wall time.

Initial read-only investigation: `I:/_triaevum_work/lighting-rationalization/`.
The archived Kokiri traces contain 17 atmospheric draws in the Scene domain;
world-lighting admission previously tested only that domain. A fixed sky region
in the archived Native capture has mean linear luminance 0.744; the authored-key
capture has zero there while its terrain receives light. The Native capture is
at a different frame, so this is diagnostic evidence, not an exact parity test.

## Development record

2026-09-26: implementation scoped; baseline worktree snapshots retained privately
for selective commits. No implementation or new runtime qualification is claimed
by this initial record.

### Appearance increment

Surface replacement now requires an explicit OpaqueWorld or TransparentWorld
owner, both when preparing lighting and when resolving the pipeline variant.
Atmosphere and unknown ownership keep their native appearance.

The first runtime probe exposed a second, upstream defect: the configured sky
producer queues models rather than emitting their CMB packets immediately.
Native CMB pass 0/1 alone consequently mislabeled those later packets as world.
The title adapter now carries the producer attribution on the exact eight-byte
native queue slot. It observes `0031487C` (model in r2, slot in r1); the native
`002FAD2C`, `002FAE10` and `003FE374` consumers identify that slot with r8+r4*8
at their CMB calls. These registers were verified against the native ARM code
and the read-only reconstructed producer. Model identity is deliberately not
the key: another queue can submit the same model without inheriting its role.
Slot replacement and a new gameplay frame invalidate old ownership.

Verification:

- 42 shader-pipeline/instrumentation tests passed, including world/atmosphere/
  unknown admission using the same program contract.
- Native composition regression passed, including deferred opaque/transparent
  consumption, the same renderer in another slot, slot overwrite and frame reuse.
- Deterministic Kokiri source sample 111550, host frame 30, 1280x720:
  sky ROI `(620,5)-(780,30)` changed from black to the exact Native RGB values
  with every native light gain zero and the authored key enabled. All 4,000
  ROI pixels match; mean linear luminance is 0.7385199023.
- Native framebuffer SHA-256 remains
  `a12f2ecfc6bc30d7e5f2d78be74b309f3f0d92b5c177745edb6a79a836ac3f89`
  before and after both the admission and deferred-ownership changes.
- These are renderer framebuffer readbacks, not desktop screenshots. Grass
  remained enabled in both captures. World brightness is intentionally not
  claimed fixed by this increment; its independent reference is the next step.

### Independent brightness reference

F1 Environment now owns **Preserve scene brightness**. It retains the native
ambient/material reference independently of all three native source gains.
For sky-lit baked surfaces, the later [outdoor calibration](BAKED_SURFACE_LIGHTING.md)
distributes this reference by orientation and visibility instead of adding
another full-strength sky contribution. The baseline-only semantics and
historical measurements below otherwise describe the earlier implementation.
Authored unlit primary color also remains a base appearance, not a light gain.
Added ambient, key/fill and point lights remain additive and independently
controllable. This deliberately preserves a baseline, not total irradiance:
there is no image histogram, guessed light direction, automatic exposure,
arbitrary minimum RGB or promise of identical world pixels.

The CPU resolves direction-independent vertex ambient, including material
reflectance and the authored ambient policy, once per surface input. Per-pixel,
vertex-lit PBR and the Grass surface-lighting atlas consume the shared
`ResolvedAmbient` value directly. Fragment-lit PBR retains its native global
ambient without running the attenuated original-light loop when preservation
owns the baseline; its ambient gain is resolved on the CPU.
No original direct source is required to recover either baseline. Disabling
preservation restores authored-only lighting, including intentional black.

Environment schema 4 persists `preserveSceneBrightness`. New profiles enable
it. Versions 1–3 keep their authored-only choices and are not rewritten on load.
The native ambient slider is inactive while the reference owns that baseline;
its saved gain is retained. Native/Native+PBR, Authentic and F2 continue to bypass
the authored profile.

Verification:

- 24 production GPU source/reference cases pass, including all native gains
  zero, retained unlit appearance, authored-light response on that appearance,
  dark material channels, updated native ambient and disabled point groups.
- Production PBR GPU probes pass for triangle lists, indexed strips, alpha/base
  invariance, tangent frames, normal/ORM/emission/height and seam boundaries.
- 42 pipeline/instrumentation tests and 6,648 real F1 widget assertions pass.
- At sample 111550, the fixed `(0,0)-(800,650)` framebuffer region measures
  mean linear luminance 0.302808 Native versus 0.304710 with native gains,
  authored ambient and key/fill all zero and the reference enabled. With the
  authored key enabled it measures 0.322381. These regions are manual probes,
  not semantic world masks or a claim of brightness parity in every scene.
- Sky remains exact in both reference captures. All runs use private profiles;
  textures, material packs, active profiles and the running user session are
  untouched.

### Shared evaluation increment

- Vertex-native ambient is no longer uploaded separately for each selected
  light or accumulated again per fragment. The shared std140 block shrinks
  from 1,472 to 1,440 bytes.
- A validated camera snapshot owns the forward rows and inverse linear
  transform. The renderer reuses it only when the captured matrix is equal;
  no camera-independent or previous-frame illumination is cached.
- Disabled selected-native sources are discarded only after extracting their
  independent ambient. Zero-intensity authored sources skip preparation and
  evaluation; PBR skips zero-radiance scene sources before the BRDF.
- Identical adjacent surface blocks reuse their uniform allocation. A bounded
  CPU snapshot avoids reading potentially write-combined upload memory.
  Frame reuse and uniform-arena replacement invalidate the cached offset.
  Grass keeps the same declared block and lifetime as its source draw.
- Fragment-lit PBR with zero active native lights is not mistaken for authored
  unlit appearance. Its global ambient follows the same policy.

Verification:

- The exact staged headers, evaluator and 22-case committed fixture also pass
  on the GPU; pre-existing working-tree material tests remain separate.
- 42 pipeline/instrumentation tests and native composition regressions pass.
- All 24 working-tree GPU source cases pass. Additional throwaway production
  PBR vertex/fragment probes verify the reference with original gains zero,
  intentional black with reference Off, a changed native ambient snapshot and
  unchanged alpha. The pre-existing untracked material fixture is not adopted
  into this change.
- Native, reference-only Per-pixel, authored Per-pixel and authored Per-pixel+PBR
  framebuffer SHA-256 values are exactly unchanged by this optimization.
- Native savestates before/after are semantically equal, with zero differing
  guest bytes; even the normally ignored memory write generation is equal.
- Steady-state timing qualification is recorded separately below. Reduced
  arithmetic and block size alone are not an FPS claim.

### CPU-resolved ambient policy

The final increment also folds the reference/native gain and authored ambient
addition into the CPU's `ResolvedAmbient`. Vertex-lit Per-pixel, PBR and Grass
no longer branch on the environment policy or combine constant ambient RGB
per pixel. Native-fragment PBR still accumulates pixel-dependent native ambient
when requested, then applies the CPU-resolved gain and authored ambient once.
An inactive environment supplies unit gain; preservation does not overwrite
the profile's saved native-ambient strength.

The production builder and vertex/fragment PBR shaders were exercised together
with native gains zero, reference Off, environment disabled, changed native
ambient, and mixed half-strength native plus authored ambient. The mixed case
checks that the policy is not applied twice. Both shader paths preserve the
tested material alpha. All 24 working-tree GPU cases, 42 pipeline/instrumentation
tests and native composition regressions pass after this increment.

Final runtime verification of this increment:

- Eight framebuffer SHA-256 values match their pre-increment baselines exactly:
  Native, reference-only Per-pixel, authored Per-pixel, authored Per-pixel+PBR,
  Authentic, F2, and Link's House with the reference On and Off.
- The Native serialized state is entirely equal before/after this increment.
  Native frontend and submission state also match across the three qualified
  non-native surface-lighting modes.
- The exact staged headers/evaluator and committed 22-case GPU fixture pass.
  The actual-widget F1 smoke passes again with 6,648 assertions.

## Deterministic qualification

Qualification uses the actual Windows/Vulkan runtime on an RTX 3060 and
i5-12400F, private environment profiles, framebuffer readback, serialized PICA
targets and native submission diagnostics. It does not use desktop screenshots
as fidelity evidence. No game-derived captures or checkpoints are committed.

### Outdoor scene and canonical boundaries

Kokiri scene `0x55` uses `fairies-visible.oot3dsav`, source sample 111550,
host capture frame 30, at 1280×720. Native is byte-identical to the original
pre-change runtime:

`a12f2ecfc6bc30d7e5f2d78be74b309f3f0d92b5c177745edb6a79a836ac3f89`

Native, reference-only Per-pixel, authored Per-pixel and authored Per-pixel+PBR
retain identical native-memory target payloads and presentation depth. Guest
memory, native PICA frontend and native submission state also remain unchanged.
Across the four structural traces, all 3,484 native draws retain their order,
raw output-merger registers and raster state. The trace digest is:

`a173d058edf5a2506b345fe2d0d21883b8906c7b1a25ae96495726b3855cd8c4`

The trace contains 589 atmosphere, 1,547 opaque-world, 775 transparent-world,
476 UI and 97 unknown draws. The bottom-screen UI target is unchanged.
Stencil is inactive in this scenario: zero enabled draws and zero serialized
stencil bytes. This proves unchanged disabled stencil state, not live stencil
output under an unexercised scene.

The 4,000-pixel sky probe `(620,5)-(780,30)` is exact Native under all qualified
surface-lighting variants. Its mean linear luminance is 0.738520. World shading
is deliberately not pixel-identical: the wall probe changes from 0.180393 Native
to 0.126651 with only the ambient reference, then 0.133034 with the authored key.
Preservation is an ambient/material baseline, not normalized total irradiance.

### Authentic and F2

Authentic and F2 framebuffers are byte-identical across the shared-evaluation
optimization. F2 retains its existing extension suppression mask and suspends
the authored environment; it does not convert Per-pixel modes to Native.

Compared with the original pre-change F2 runtime, 31,969 pixels intentionally
change, all inside the half-open bounding rectangle `(0,0)-(1098,77)`.
The sky probe changes from linear luminance 0.783748 to exact Native 0.738520.
The qualified wall and ground probes are unchanged. Guest memory, native
frontend/submission and native-memory target payloads are unchanged. Thus the
whole original F2 framebuffer is **not** claimed identical: it inherits the
sky-attribution correction.

### Interior and explicit darkness

Link's House, scene `0x34`, uses `timing_link_house_11800.oot3dsav` with an
explicit private scene-`0x34` profile. All native source gains, authored ambient
and key/fill intensities are zero:

| Manual framebuffer region | Native | Reference On | Reference Off |
| --- | ---: | ---: | ---: |
| Room `(0,270)-(1280,720)` | 0.072517 | 0.065982 | 0.001604 |
| Bed `(430,570)-(920,650)` | 0.255480 | 0.255523 | 0 |

Values are mean linear luminance, not semantic masks. Reference On keeps the
room visible; Off permits intentional black. Native-memory targets and
presentation depth remain exact. The earlier `house-PerPixel` capture had no
scene-`0x34` profile and is only an unprofiled baseline, not reference-On proof.
Changed native ambient is also exercised directly through the production
builder and both PBR shader paths; a live day/night traversal is not claimed.

### Timing limits

The shared-evaluation increment was measured with four before/four after
throughput runs, 120 warmup plus 240 measured frames each, pacing and VSync
disabled, without screenshot or semantic-trace capture. Trial order was A/B,
A/B, B/A, B/A. No pipeline was created in the measured windows.

| Trial | Before mean frame, ms | After mean frame, ms | Before backend CPU median, ms | After backend CPU median, ms |
| --- | ---: | ---: | ---: | ---: |
| 1 | 30.6091 | 29.1280 | 9.6216 | 7.5743 |
| 2 | 29.4482 | 29.3918 | 9.0876 | 7.6947 |
| 3 | 28.0692 | 28.7416 | 6.1783 | 6.2345 |
| 4 | 27.9103 | 28.0027 | 6.0697 | 5.9552 |

GPU-frame medians span 4.5376–5.2578 ms before and 5.2494–5.6574 ms after.
These runs do **not** establish a stable FPS gain. The user's running session
was left alone, so the machine was not isolated; neither the apparent early
CPU improvement nor the GPU timing difference is assigned a causal speedup
or regression. The later CPU-ambient increment removes constant per-pixel
policy work, but arithmetic reduction alone is not a throughput guarantee.

Two additional final-build throughput runs used 120 warmup plus 120 measured
frames each, under the same non-isolated conditions:

| Trial | Mean frame, ms | Backend CPU median, ms | Upload median, ms | GPU-frame median, ms |
| --- | ---: | ---: | ---: | ---: |
| 1 | 29.0004 | 6.0836 | 0.4129 | 6.2469 |
| 2 | 30.9292 | 6.4024 | 0.4370 | 5.8146 |

Neither window created a pipeline. These shorter, later runs are not paired
controls and do not establish an FPS improvement for the final increment.

### Profile migration and evidence location

For an existing schema-1–3 profile, enable **F1 → Environment → Preserve scene
brightness** explicitly. Migration preserves the old authored-only choice;
it does not silently edit the user's profile. New profiles default to On.
Turning it Off restores intentional-dark workflows and the saved native
ambient gain.

Private evidence is retained under
`I:/_triaevum_work/lighting-rationalization/`: `qualification-report.json`,
per-capture receipts/configurations/framebuffers/diagnostics, and the final
CPU-ambient parity and qualification reports. Temporary analysis sources are
removed after verification; captures and recovery baselines remain private.
