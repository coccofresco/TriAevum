# MM3D environment-profile parity qualification

Local-only branch `mm3d/parity-gp`, based on `18ef2d79`.

The original qualification below is historical. See **Final dff87fdf recapture**
for current images/numbers and the failed fresh-native Inn gate. The preceding
recalibration record explains the ambient ledger and secondary-material fix.

## Implementation

- `84150bb7`: design before code, `docs/NATIVE_FRAGMENT_ENVIRONMENT_PROFILES.md`.
- `0e7a53b7`: title-neutral native-fragment balance, shared ambient/key/fill/rim/
  fog/placed-light evaluation, F1 availability and boundary/compiler tests.
- `1026eb3a`: six shipped group/day/night files in `environments/mm3d`, real
  release-loader tests, missing-file fallback and time-at-entry night selection.

No new guest addresses, per-scene profiles/lists, material/texture exceptions,
image-derived renderer policy or baked-reference admission were introduced.
The native TEV vertex-colour/AO stage and coverage replay remain separate.

## Evidence (not checked into Git)

`W` = the local work directory (not in this repository), evidence `W/analysis/parity/GP/`:

- `captures/INDEX.md`: six native-faithful/profile pairs, AO-corner reuse,
  mean RGB/luma changes, individual process outcomes.
- `CONTROLS.md`: every Lights & Environment control, support boundary and
  remaining dependencies; not a claim of an individual game run per slider.
- `INTEGRATION.md`: precise merge seam with dynamic-light work.
- `PROGRESS.md`, `O1-M1.log`, `O1-nri.log`, `O5.log`, `O6-*.log`, `O7-build.log`.
- `runs/<case>/`: receipt, logs, original BMP hash and reduced diagnostics.

Developer game binary: `W/host-build-gp/oot3d_native_game.exe`, SHA-256
`036e4a74c8de9b1010a3f7209b1aeeee02cb4eafa8c5eb251b79f31872d2de8c`.
Clean source `1026eb3a`, registry v10, own patched `nri-src-gp`, iMMERSE and DLSS
present, explicit GP resource root and dekui RomFS overlay. O7 replaces this
executable afterwards; no large developer executable snapshot is retained.

## Gates and runs

- **O1/M1 passed:** pipeline 68; foundation 463 plus one skipped; environment
  655582 checks; MM3D profile 94; MM3D lighting/environment/composition/dev-tools
  and native composition suites; NRI patch-set CTest.
- **O5 passed:** regenerated native 24, temporal 40, toon 348, uber 36, caster 6
  and built-in pass 104 modules; every old pack byte-identical. Four production
  balance variants compile separately (not part of the old uber family).
- **O6 passed:** actual widgets, full 12516 and scene environment 2720 assertions.
- **O7 product build passed:** in-place configure with developer tools/tests OFF,
  full build plus `oot3d_native_game`; `--product-info` reports
  `developer_tools: false`, source commit `1026eb3a`. Product binary SHA-256
  `1570a743a7c53865cef86e953072acc416d9efa80371665aa2900a38d38a6061`.
- **O2/OoT3D build not run**, as requested; coordinator must gate after merging.

Exactly **13 game launches**, all through the private lab run harness (not shipped): twelve distinct
paired capture cases plus one final interpolated smoke; no retries. The twelve
cases already had receipts on session resume, so no game was launched on resume.
Eleven capture processes exited ok. `night_native` saved its valid frame 270,
then failed with `TRIAEVUM_FATAL bad allocation`; its process gate is **failed**.
The one final smoke failed before capture: shell resource/fork error
`0xC000012D` and `vkAllocateMemory(buffer)` result `-2`. Widget O6 is not a
substitute for that **failed game smoke**. No other processes were stopped.

Full-frame mean RGB change (profile minus native-faithful, 8-bit units):

| Noon | Night | Field | Stone Tower | Inn | Woodfall |
|---:|---:|---:|---:|---:|---:|
| -1.40 | +7.08 | -2.07 | -10.09 | -32.95 | -14.73 |

Noon/field sunlit brightness stays roughly native and shadows are clearer.
**Indoor look calibration is not final/accepted:** OoT3D's starting indoor
ambient gain 0.4 makes Inn/Woodfall substantially darker. Owner review is still
needed; no scene-specific compensation or second look batch was introduced.

## Interior recalibration follow-up (pre-final-capture record)

Indoor and TimeFlows **day** now retain the exact native per-draw ambient
(`nativeAmbientStrength=1`, added ambient zero), instead of removing 60% or
replacing a draw-dependent budget with a constant material term. Key, local,
rim, fog and shadow settings and all night files remain unchanged. TimeFlows
is shared: the noon/field example's ambient changes from 0.224 to native 0.247;
no byte-identical outdoor-profile output or new outdoor capture is claimed.

The one requested three-location batch exposed the shelf figure's actual
failure: its native TEV multiplies `FPRI * texture * vertex tint * FSEC`.
The profile zeroed Scene secondary while replacing only primary with Lambert,
so ambient could not unblack the material. A structural TEV proof now retains
captured Scene secondary/LUT material factors, without shadowing them twice;
Local secondary still follows its native-local control. Additive, inverted,
alpha-only, overwritten and primary-only cases keep the old policy. No actor,
texture or scene exception; no guessed MM3D identity from N64 tables. Only the
balance variant advances to V3; OoT3D's capability-false route is unchanged.

Affected gates passed: pipeline **69**, real-file MM3D profile **110** checks,
production balance shader compiler **4** variants, actual-widget O6 **12516**
and **2720** assertions. No broad unit matrix/pack regeneration, O2, O7 product
qualification or additional game smoke was rerun. Current host-build-gp is back
to **developer/tests ON**, not the historical O7 product configuration.

Exactly **six new attempts**, no retries. All processes exited zero, but
Woodfall native lost its swapchain before capture (`VK_ERROR_OUT_OF_DATE_KHR`),
so there are only **five new images**. Its sheet explicitly reuses the previous
native reference. Ambient-only deltas: Inn -4.74, Woodfall +2.20 (historical
reference), Stone Tower -7.35; selected ROI losses remain visible in metrics.
**All these pictures precede the structural secondary-factor fix. At that point
V3 shelf appearance was not visually verified**, and no look acceptance was claimed.
The run budget was not exceeded to hide this gap.

Evidence: `W/analysis/parity/GP/captures/recal-1/INDEX.md` and
`W/analysis/parity/GP/recal-1/` (diagnosis, captured input/TEV proof, receipts,
post-fix binary hash, affected-build/compiler/unit/O6 logs).

## Final dff87fdf recapture

Used the unchanged, qualified `dff87fdf` developer executable, SHA-256
`8c7445e941a4efa82e2c2409331b9d63677bd1560d785939654498a95c6dca48`.
Only Inn, Woodfall and Stone Tower; one process at a time, six attempts, no
retries, no extra outdoor/night/smoke captures or build/test runs.

Five processes/images succeeded. **Inn native failed before rendering** because
`analysis/parity/3/work5/state_inn.oot3dsav` could not be opened. The input was
available for the later, never-attempted Inn profiles case, which succeeded.
The failed native case was not repeated. Inn's comparison explicitly uses the
previous native reference; Woodfall and Stone Tower are fresh pairs.

| Location | RGB delta (8-bit) | Luma delta (8-bit) | Native reference |
|---|---:|---:|---|
| Inn | -4.72 | -6.13 | historical raw aggregate / JPEG |
| Woodfall | -7.16 | -9.25 | fresh final-build image |
| Stone Tower | -7.35 | -7.70 | fresh final-build image |

The **final profile image visibly restores the shelf figure's colour/detail**,
not the previous black silhouette. Its region is about -5.66 versus the prior
-33.85 (historical-JPEG spatial comparison, approximate). Four captured prop-part
records confirm applied V3 material-factor flag and Scene secondary gains=1.
This is visual/profile-path verification, not a passed fresh-native Inn pair.

Remaining look limits are explicit: Woodfall floor ROI -19.40 and Stone Tower
floor -37.73 remain darker. Full-frame means do not prove every deficit belongs
only to correct shadows; no blanket look approval. The fresh-native Inn process/
capture gate remains **failed** under the no-retry instruction.

Updated evidence: `W/analysis/parity/GP/captures/INDEX.md`,
`captures/recal-2/INDEX.md`, lossless PNGs/annotated comparisons/metrics and
`recal-2/runs/` receipts plus `material-factor-proof.json`. Earlier passed unit/
compiler/O6 gates still apply to the unchanged code; none were gratuitously rerun.

## Remaining work

Native-local dynamic replacement, native point receivers, emitter/emission
catalogs and grass belong to the other lanes and are not claimed solved here.
Scene-light rim currently follows shared added lights, not a new reconstruction
of retained native-slot scattering. Interior room shells, full native-fragment
PBR material coverage, weather attribution, live FixedTime/TimeAtEntry fixtures
and native-oracle comparison remain outside this qualification. GI is excluded
by owner decision. No product gameplay run was added.
