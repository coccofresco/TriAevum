# LIGHTPERF wave 2: partial qualification

Baseline `3fb5b0c2`, branch `mm3d/parity-lightperf`. Evidence:
`I:/_triaevum_work/mm3d-port/analysis/parity/LIGHTPERF/wave2/`.
Gap numbers below are **LM/GAPS**, not the first wave's GRASS optimization IDs.

## G09 — native-fragment normal guide selection

Ordinary native-fragment operand/shadow variants previously selected the
vertex-surface normal whenever surface lighting was enabled, ahead of the
frontend's `MaterialNormal` contract. They now retain the native decoded `normal`
for the normal guide. This is the frontend's view-space quaternion/bump result;
we neither substitute a constant axis nor reinterpret the raw quaternion as RGB.
The choice requires the supplied semantic contract, salts the specialized key,
and uses the existing Material decision in the uber program. Without that
contract the old surface fallback remains. Native TEV/color/alpha, geometric
face-guide production, vertex-lit paths, GP profiles and anchor tolerances are
unchanged. Installed GGX still writes `pbr_normal`; its reconstruction is **not
repaired or visually certified by this change**.

`build_fragment_artifacts --normal-guide-check` compiles 12 production frontend
and instrumentation variants: native unchanged, operand, shadow, installed-GGX
writer, and contract-missing fallbacks, with/without native lighting LUTs. It
checks decision selection and fallback key isolation. These are synthetic
shader/compiler tests, **not actual MM geometry execution**. Of the eight
positive cases, four change Surface→Material; two native and two GGX writers
remain unchanged. Four negative cases preserve Surface. All six legacy pack
families were regenerated and checked byte-identical, including uber/pass.

Cheap diagnostic counters distinguish native decoded-normal guide draws and
PBR-normal guide draws. They count selected writers, not normal quality, GPU
execution time, or cache savings. Diagnostics tests cover JSON serialization
and disabled operation.

### Bounded run outcome

The one MM targeted attempt exited 1 before loading: the owned build had retained
`TRIAEVUM_TITLES=oot3d` from the prior wave's final gate. The lane configure recipe
had not overridden the cache. Its launched SHA256 was
`710d785f0cb96e3476b6a848cf770dadf766ff58b9ef7e4e0eb462bc8ef6b860`.
The recipe now explicitly selects `oot3d;mm3d` (this tree requires oot3d in the
shared window session), and the corrected dual-title build succeeded. There was
**no MM retry**. The attempted product-info file is empty and provides no proof;
no corrected-MM binary hash was retained before the later OoT build replaced it.
No floor/wall debug image, AO appearance verdict, live counters or speedup can
be claimed. G09 remains partially addressed, not closed.

The separate OoT smoke/O2 gate exited 0, 400 frames, with frame 100/200/300 all
matching `cap-p13.sha256`. Binary SHA256:
`3aedfa2e3c75e98af76ae0e654f70a3588201efc8954179c4c4a73ecfc94e307`.
The owned build's final configuration/launcher is OoT again. Offline results:
foundation 463 passed + one optional room-fixture skip; pipeline 71 passed;
diagnostics 6 passed; normal-guide compiler cases 12 passed. Exactly two safe-run
attempts, only one successful game execution. No confirmation runs.

## G13 — EUR scratch arena facts, not a new admission policy

Meaning references (read-only): zeldaret-mm `src/code/z_lights.c` and 2Ship
`mm/src/code/z_lights.c`, `LightContext_NewLights` / `Lights_New`: allocate a
light group through the graphics arena, initialize ambient color and zero count.
EUR image SHA256:
`6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d`.
`offline_ownership.py` checks that hash plus instruction-word guards before
reading the existing noon/night/Inn savestates.

- `0x001EFC7C` reads `r1+0x4C`, subtracts **0x100 bytes**, writes the cursor back,
  initializes duplicated ambient bytes and zeroes the two leading halfwords.
  `0x001BFB40` initializes a caller-supplied record instead of allocating.
- Actor path `0x00182898` obtains graphics context from `Play+0x100`; call
  `0x001828A0` allocates, `0x001828BC` binds lights, `0x001828CC` applies the group.
  A BL-word scan finds eight static callers of the allocator, including scene
  and actor paths. That is not eight allocations/frame or a per-actor maximum.
- `0x001067C0..0x001067D8` selects a backing buffer using the low bit at graphics
  context +0x60, stride **0x1FE00**, and initializes graphics context +0x40 with
  **0x19C00 bytes**. Literal base at `0x00106B90` is `0x0072F91C`.
- Header initializer `0x001393A0` stores size/start/head/end. Thus the +0x40
  header's fields are size +0x40, base +0x44, head +0x48, tail +0x4C.
  The two observed bases are `0x0072F91C` and `0x0074F71C`.
- Empty capacity is **412** 256-byte records, **not OoT's 128**. This is only an
  empty-arena ceiling, **not an actor admission limit**. Other consumers and
  head growth reduce it. Naive allocation 413 crosses the base by 256 bytes.
- Saved-state free bytes noon/night/Inn: **102400 / 105472 / 99328**; corresponding
  remaining whole-record ceilings **400 / 412 / 388**. These are snapshots at
  different phases, not live high-water marks or allocation counts.

No guest writes, culling changes, fixed budget, allocator interception or
production guard was added. Full consumer census, extra per-actor allocation
bounds, reset/liveness across deferred queues/save-load, high-population canaries
and native/extended fallback remain unqualified. **G13 remains open.**

## G10 / G26 — explicit limits

The existing MM queue generation is reset per scene build; actor id+address is
lighting attribution, not a spawn-generation temporal identity. No such pair
was promoted to `TemporalIdentity`. Model/part/slot lifetime, address reuse,
room/sky identity and coherent camera/model queue phase remain unproved.
Three saved views map their stored eye to approximately the origin (errors
5.88e-5 / 3.44e-5 / 1.29e-5 world units); this only validates each snapshot's
camera convention, **not temporal or model-phase coherence**. No anchor or
static-cache churn improvement is claimed. **G10 remains open.**

G26 gains writer attribution only. There is no new MM p50/p95/p99 data, successful
module-complete live window, Android measurement or throughput claim. No other
lanes, guest IDs, GP values or unreleased optimization branches were changed.
