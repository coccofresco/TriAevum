# Startup family isolation regression (2026-11-07)

`eff80eb6` appended 104 native-lighting supersets to the 36 legacy programs.
The startup caller consumed the whole combined table even when the installed
title did not enable native-fragment lighting. INTEG's fixed cut included that
commit but not the capability restriction subsequently added in CATALOGS
`f0a967ec`. Its OoT Kokiri log prepared 159 parts rather than 55: 241.394 s
startup / 240.450 s waiting, despite accepting the original 288,985,245-byte
NRI cache. This is unused driver pipeline work, not a switch to Complete mode
or a changed cache filename.

`StartupUberFragmentArtifacts` now selects the legacy 36 unless either native
fragment operand or native fragment shadow capability is installed. Full
140-program lookup remains available to draws; neither admission nor shader
equations change. The currently OoT-only Forge helper uses the same selection.
When integrating the publisher-family helper from `f0a967ec`, preserve its
native-family selection for an additional title pack rather than forcing all
helper invocations to legacy mode.

A second, older issue was stale embedded vertex identities after `d37c62fb`'s
clip-scale uniform wrapper. Import the independently qualified `c7d57e76`
artifact repair together with this fix (or retain it if already integrated).
Never loosen source-identity matching. The original OoT cache filenames and
Hybrid policy remain untouched.

## Regression gates

- `renderer_spirv_pack_tests`: `StartupFamilyFollowsNativeLightingCapabilities`
  checks all four capability combinations. With the embedded vertices, D32,
  one sample and geometry available, non-native startup must contain 36
  supersets / 55 parts; native startup retains 140 / 159. Every link is valid.
  This catches work growth even when a warm driver cache hides the delay.
- The host test build now exposes `triaevum_build_vertex_artifacts`. Run
  `--check <private CmbVShader.shbin> <private profile.shbin>` to compare the
  generated source identities and derived pass modules against the embedded
  pack. No ROM input is distributed or required in normal user installation.
- One deterministic Kokiri O2 execution must retain all three cap-p13 hashes,
  Hybrid and the original cache path, 55 startup parts, no fragment artifact
  misses and no unavailable uber categories. Do not compare timings without
  recording application-cache bytes and driver-cache limitations.

Local measurements, binary receipts and unrun boundaries are recorded in
`W/analysis/parity/OOT3DPREP/RESULTS.md`. No profile values or title facts change.
