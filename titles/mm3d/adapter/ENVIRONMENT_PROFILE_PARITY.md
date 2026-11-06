# MM3D environment-profile parity qualification

Local-only branch `mm3d/parity-gp`, based on `18ef2d79`.

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

`W = I:/_triaevum_work/mm3d-port`, evidence `W/analysis/parity/GP/`:

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

Exactly **13 game launches**, all through private `run_safe.sh`: twelve distinct
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

## Remaining work

Native-local dynamic replacement, native point receivers, emitter/emission
catalogs and grass belong to the other lanes and are not claimed solved here.
Scene-light rim currently follows shared added lights, not a new reconstruction
of retained native-slot scattering. Interior room shells, full native-fragment
PBR material coverage, weather attribution, live FixedTime/TimeAtEntry fixtures
and native-oracle comparison remain outside this qualification. GI is excluded
by owner decision. No product gameplay run was added.
