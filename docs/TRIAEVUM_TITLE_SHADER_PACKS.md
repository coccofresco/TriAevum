# Optional portable title vertex programs

This extends, not replaces, the existing precompiled release contract.
Publisher tooling can generate an additional SPVK pack from local native title
SHBINs using `build_vertex_artifacts OUTPUT --translate INPUT...`. Translation
is publisher-time only. Ship neither the SHBINs nor the extracted RomFS. The
pack contains portable vertex SPIR-V, canonical shader identities and derived
vertex passes; no driver cache. Unsupported non-vertex DVLEs fail generation.
Do not silently publish partial output as all-title coverage.

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
