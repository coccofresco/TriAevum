# Optional portable title vertex programs

This extends, not replaces, the existing precompiled release contract.
Publisher tooling can generate an additional SPVK pack from local native title
SHBINs using `build_vertex_artifacts OUTPUT --translate INPUT...`. Translation
is publisher-time only. Ship neither the SHBINs nor the extracted RomFS. The
pack contains portable vertex SPIR-V, canonical shader identities and derived
vertex passes; no driver cache. Non-vertex DVLEs fail generation by default.
For a mixed-stage SHBIN, explicit `--vertex-only` generates just the vertex
scope and reports each untranslated geometry DVLE and the final stage counts.
Unknown stages and an empty vertex family still fail. This does **not** lower
geometry shaders or prove all-title coverage. Preserve the full input/stage
census and the unresolved geometry gap with the artifact.

To check that scoped output, set `TRIAEVUM_TITLE_PROGRAM_PACK=OUTPUT` and run
`build_vertex_artifacts --check --translate --vertex-only INPUT...`. This checks
against that explicit pack, not an embedded-first fallback, and requires exact
vertex/pass coverage counts. Unscoped `--check` retains the existing behavior.

`--title-program-pack PATH` selects the optional runtime pack. Entries are
appended **after** the legacy embedded vertex/pass artifacts, preserving OoT
lookup precedence and the no-option behavior. Owned aligned storage keeps
views valid. The loader bounds file/entry/instruction extents and requires
vertex execution models; this structural check does not replace publisher
SPIR-V validation or live qualification. An invalid selected pack fails loudly.

Native Forge and its Python oracle accept the optional `title_program_pack`
file record in a precompiled-title entry, verify its byte count/SHA, route its
path into the installed launch profile, and record/recheck its installed hash.
A damaged packaged file fails activation; it is not regenerated on the player's
machine. No MM supported-revision entry or qualified pack is implicitly added.
Device pipeline preparation still needs its separately qualified manifest/AOT
pack; this option does not assert whole-game or zero-runtime-compilation coverage.

## Native-lighting always-available family

`pica_native_lighting_uber.spvpack` is a separate maintained-equation family,
not a ROM-derived title pack. Generate with `build_fragment_artifacts
--native-lighting-uber OUTPUT`; `--check` verifies source coverage. Optional
`--reuse-builtins` reuses exact source-identity matches from the generator's
embedded artifacts (record that input pack/toolchain when used).

Its 104 unique programs cover raw fragment operands/balance/native shadows
(64), balanced native GGX (16), and native-unlit GGX (24), including temporal
interfaces, integer texture zero and primitive-ID/no-primitive-ID variants.
The original 36-program pack is unchanged; registration appends the new family.
Native-policy startup uses the combined 140 programs. Titles without native
lighting policy retain the original 36-program startup set; lookup still
preserves embedded-first precedence.

The superset uses the same surface-lighting, capture and GGX ownership equations
as specialized shaders. In particular, balanced GGX preserves Local per-slot
energy instead of applying the scene native-direct gain again. No new title
light classes, scene rules or renderer profile values are introduced.

Selection requires exact policy equality, a valid surface interface and typed
native capture sites where needed. Invalid mixed GGX/operand policies remain
unsupported. Developer shadow probes remain outside the release family. All
five policy flags and all three interface flags participate in the artifact
cache key; replay uses the same selector/key as the renderer. Legacy programs
cannot be selected for raw uniforms. New decision bookkeeping does not change
the specialized source witness.

Tests cover all 120 native policy/interface paths (some share identical shader
source), incompatible-policy rejection, single native capture before alpha
replay, GGX gain/unlit endpoints, embedded modules and startup stage linking.
Source coverage does not substitute for live, cold/prewarm and publication gates.

### Optional-family startup routing

A non-OoT title with a publisher program pack now prepares that pack's vertex
family at startup, rather than skipping preparation or warming unrelated OoT
vertices. Without an optional family the previous behavior remains. The
headless `triaevum_nri_pipeline_prepare --title-program-pack PATH
--startup-programs` route owns/validates the same pack and builds the same
native program parts. It is not a complete room/pass/whole-title census.

Native and Python Forge validate the title's existing `title_program_pack`
artifact record before the device job, pass this option, and include
`title_program_pack_sha256` in its receipt. Missing/corrupt artifacts fail
before the helper; driver/prewarm failures retain the existing optional
acceleration policy. No optional field means unchanged OoT arguments/receipts.

Pure `uber` recordings intentionally have no portable pipeline recipes (their
programs are library parts). Their raw draws/variant requests can prove source
replay and availability, but are not complete portable-recipe sessions. The
regeneration CLI accepts complete variant-record sessions without a separate
capture import; required witness, uber, recipe and policy gates still apply.

## Exact replay context

Variant-request v1 records now include a `fragment_context` object with four
booleans: `operand`, `balance`, `native_shadows`, `shadow_probe`. All four affect
effective shader generation and are part of recorder deduplication. Partial
objects are invalid. Absence marks a legacy record, not evidence that all flags
were false. Developer probe requests are rejected in builds without that feature.

For existing archives only, the inventory tool accepts
`--recover-legacy-fragment-context` **with** `--require-witness`. It enumerates
the eight production contexts, retaining a context only when the entire
original witness matches, and reports matching masks/mismatch samples. This
cannot be combined with synthesized effects or inferred runtime state. Zero
matches remains a qualification failure. Multiple equivalent masks do not
prove historical settings; they prove only witnessed output equivalence.
New captures must carry explicit context and pass without recovery.

Qualify vertex and specialized fragment packs against the exact renderer source
and settings. Preserve the embedded OoT families and run the required OoT gate.
Observed-session coverage is not exhaustive corpus coverage; regenerate and
requalify after integrating renderer changes, including lighting ownership work.
