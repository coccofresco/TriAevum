# Operational implementation strategy

This note exists to keep OOT3D reverse-engineering work from collapsing into
gate-only loops. The project goal is implementation from native OOT3D data and
code, not exhaustive proof before every runtime step.

## Core rule

Every work unit must end in one of these concrete outputs:

- a typed native data structure/header used by runtime code;
- a runtime reader/interpreter for an OOT3D asset or `code.bin`-backed table;
- a maintained source function or split that replaces anonymous behavior;
- a focused comparator that directly protects one implemented runtime path.

If a task would only produce another broad audit, shrink it until it feeds one
of those outputs.

## Vertical slice workflow

Use this order for each subsystem or visible scene feature:

1. Pick a narrow runtime target: one actor route, one material path, one camera
   mode, one QDB command family, one scene setup, or one title-intro phase.
2. Identify the smallest native evidence set needed to drive it: asset payload,
   literal/table owner, focused Ghidra export, or already captured emulator
   trace as validation.
3. Promote only stable facts into source: offsets, enum values, table entries,
   asset references, fixed-point formats, packet layouts, and function roles.
   Keep unknown fields as named raw fields instead of blocking the slice.
4. Implement the runtime interpreter against those promoted structures. Do not
   use N64 data as runtime input and do not substitute emulator dumps for
   native data.
5. Verify with the cheapest useful check: compile, unit/headless probe, one
   screenshot or trace comparison, or object comparison for decompilation work.
6. Commit the source/tool/data contract changes together with the small audit
   that explains their native evidence.

## Completeness rule

Correctness checks are useful only if they move a visible or runtime-complete
slice forward. For scenes such as the open title, each iteration should add one
missing reproduced feature to the running demo before starting another broad
search:

- a visible actor/object that belongs to the native scene state;
- a native environment effect such as sky, sun, moon, lens, haze, fog, or clear
  color;
- a material or lighting path consumed by rendered batches;
- a camera/cutscene command that changes the presented frame;
- a native state transition that changes which assets, commands, or render
  parameters are active.

If an investigation produces only a mismatch list, immediately convert the top
mismatch into one runtime implementation slice. Do not keep collecting more
mismatches until that slice has either landed or been proven to require a
specific missing owner function.

## Gate limits

- A gate is allowed when it answers a binary implementation question.
- A gate is not allowed when it only asks for more global certainty.
- Prefer one selective Ghidra export per slice. Run another only if the first
  export identifies a concrete missing writer/callee that blocks code.
- Prefer one emulator capture per visual mismatch class. Do not repeat captures
  until the engine code changed in a way that should affect that mismatch.
- Do not re-check N64 structure at every step. Use N64 only for naming,
  expected gameplay architecture, or when OOT3D evidence is ambiguous.
- A hung or fragile visual probe is not a blocker for committing a source-level
  runtime fix when compile and a focused unit/headless check already prove the
  promoted native path. Record the probe limitation and move to the next visible
  slice.

## Evidence roles

- Native assets and `code.bin` are implementation sources.
- Ghidra exports identify structure, ownership, and behavior.
- Maintained source/object comparison proves decompilation fidelity.
- Emulator traces and screenshots validate runtime behavior and localize
  mismatches, but they do not become source data.
- N64 source is a secondary semantic guide, never a runtime backend.

## Title-intro application

For the open-title milestone, the next useful slices should be ordered by
visible dependency:

1. Source selection: choose the correct `spot00_demo_epona_*` QDB and title
   state from OOT3D code paths. Stop once the demo selects the same native
   payload; do not fully decompile unrelated input globals first.
2. Scene and actor instantiation: spawn the Hyrule Field backdrop, title logo,
   Link boy, Epona, and required opening keep objects from native actor/object
   tables and ZAR references.
3. QDB command interpreter: apply the decoded player-action, actor cue, camera,
   environment, and transition command families directly from QDB rows.
4. Actor motion binding: connect QDB cues to the native EnHorse/Link-boy action
   and CSAB tables, preserving unresolved cue fields as raw typed data until
   the consumer split proves their names.
5. Logo/update/draw path: run EnMag state, alpha, CSAB, and draw-handle
   submission through the native component table already identified.
6. Rendering parity: material lighting, fog/environment, sky, and post effects
   should be fixed on the rendered title slice, not as detached global audits.

For the current title-intro lighting work, the operational order is:

1. Drive environment/fog/clear and actor/VS lighting from the same resolved
   `0x0045DD50` runtime transition state.
2. Add visible kankyo primitives for sun/moon/lens/haze from native CTXB/TBD
   and packet/lane contracts.
3. Fill missing title actors/logo completeness from native actor bindings.
4. Then refine screenshot parity with targeted comparisons.

The current `0x005093E4` input-context work should remain scoped to source
selection. If it does not change which title-intro QDB/state is selected, it is
not the next vertical dependency for the visible intro milestone.

## Practical commit shape

Good commit:

- one native structure or table promoted;
- one runtime path consuming it;
- one focused verification artifact proving it is wired.

Bad commit:

- many new CSV/Markdown reports with no runtime consumer;
- repeated emulator/Ghidra captures of unchanged code;
- broad mismatch inventories that do not name the next source file or function
  to edit.
