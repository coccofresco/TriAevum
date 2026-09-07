# OOT3D source-native decomp handoff

## Consumer checkpoint

The source-native consumer is pinned to decomp revision
`bfc5ca73630feeebc57ea27323230225d0529069`. The complete 1,065-file canonical
surface compiles and links through the strict LLVM guest ABI legalizer. All 119
mapped target-data bindings retain their original address and all formerly
invalid `DAT_*` identities have been classified by the producer.

The current external closure contains:

- zero unresolved semantic source globals;
- zero external target-address functions;
- 9,184 registered native target callbacks;
- zero invalid or constant-like target-data identities;
- 37 declared compiler, C runtime, CTR and typed host imports.

The revision-pinned owner package is
`I:\oot3dre_work\source_integration\ded8104e66ea-6e7b2907b523`. It reports
8/8 source-closed owners, 1,216/1,216 unique closure entries and an empty
semantic worklist. Four old per-owner import acceptances remain unapproved,
but those gates belong to the superseded incremental A32-owner migration and
do not reopen source work for the complete source-native composition root.

The consumer's `nnMain` link gate reaches zero unresolved gameplay identities
and reports zero multiple definitions. Registry construction is derived only
from symbols defined by the canonical object; descriptive ledger labels and
mere declarations cannot be promoted into ABI symbols.

## Semantic global requirement

Most source globals are declarations introduced by the
`shared_n64_semantic_mass11` readability pass. Their target functions are
supported by OOT3D evidence, but the declarations do not preserve how each
value is represented by the OOT3D binary. The consumer must not guess this
mapping from the N64 name.

For every unresolved semantic global, the producer should emit exactly one of
these contracts:

1. A target-data alias, for example
   `extern float gName __asm__("DAT_00301234");`.
2. A typed constant definition when the value came from an ARM immediate or a
   literal that is intentionally detached from writable process state.
3. A normal owned global definition when the value is runtime state created by
   the source implementation.
4. A documented platform import when ownership belongs to CTR middleware.

Aliases must retain the OOT3D address and type evidence. Constants must retain
the source entrypoint/instruction or Ghidra evidence used to recover the value.
N64 source may explain meaning but is not sufficient evidence for address,
representation or value.

## Generated evidence report

Run the consumer preparation through `-BuildLlvmLegalizedSurface`, then run:

```powershell
python tools\oot3d\source_native_runtime\analyze_source_global_evidence.py `
  --clang I:\oot3dre_tools\llvm-22.1.6\bin\clang.exe `
  --decomp-root I:\oot3decomp `
  --snapshot-root build-source-native\decomp-snapshot `
  --snapshot-manifest build-source-native\decomp-snapshot\source_snapshot_manifest.json `
  --surface-contracts build-source-native\llvm-legalized-surface\source_surface_contracts.json `
  --output build-source-native\llvm-legalized-surface\source_global_evidence.json
```

The report uses Clang AST references, promotion manifests and their declared
Ghidra target evidence. For each unresolved global it lists every semantic
function using it and the target-data symbols present in the corresponding raw
OOT3D function. Candidate sets are investigation bounds, not automatic alias
decisions: one literal-pool symbol may feed several reconstructed constants,
and some values are ARM immediates absent from the raw Ghidra `DAT_*` set.

At `b69bb4e`, the generated evidence report contains zero unresolved globals,
zero affected functions and zero parse failures.

## Typed CTR boundaries

The reachable source now uses explicit host hooks for its kernel operations.
The consumer contract contains 37 imports in total, including the typed CTR and
host hooks. The producer removed 105 dead generic SVC declarations; neither a
defined nor an undefined `software_interrupt` symbol remains in the legalized
archive. Future producer changes must continue to expose typed calls instead of
reintroducing machine-register-dependent boundaries.

The producer must preserve all input/output registers as typed parameters and
return structures. Do not expose another variadic or register-less SVC shim.
Until these hooks exist, the consumer will not fabricate synchronization
objects or successful waits.

## Complete native runtime tranche (2026-08-03)

The complete 1,065-source archive now builds and links from `nnMain` with a
9,000-plus-entry target-function registry. The host contract has 37 explicit
imports and enables the typed service-manager, resource-limit, system-tick,
thread-pointer, actor-owner and SystemArena wait/wake boundaries together.

Guest `CreateThread` now allocates a distinct mapped TLS page, resolves its
native entry through the canonical registry and runs it with thread-local CTR
host services. Router handles and kernel event/mutex/semaphore/thread state are
protected for concurrent guest execution. A five-second launch with the owned
`code.bin` remained active at low CPU use; the focused runtime suite passes.

At producer revision `bfc5ca73`, the complete archive has zero unresolved
semantic globals and zero external `target_function_address` symbols. The
indirect registry records 9,184 callable native targets and separately proves
the 93 repeated MobiClip internal control-flow entries are absorbed by the
native decoder rather than exposed as ABI callbacks. The preparation workflow
now rebuilds and validates the legalized archive before linking the executable,
preventing a stale-revision archive from producing misleading link failures.

The host seals the registry after exactly 9,184 registrations. Further writes,
early resolution or a mismatched count abort instead of silently accepting an
incomplete callback surface. The archive audit also contains no `Fallback`,
generic SVC, A32 executor, Unicorn or A32 trampoline symbol. Target-exact
return/no-op routines remain source behavior, not host fallback dependencies.

The same archive now consumes the producer's eight shared high-fan-in
contracts, covering 3,413 calls in 406 calling units after removal of 1,020
conflicting local declarations. Its regenerated evidence remains at zero
unresolved semantic globals; the executable-closure audit passes all 13 checks
and the focused runtime suite passes.

The hard-float frontier additionally certifies 3,968 calls across eleven
high-volume ABI families and has removed 960 permissive declarations. All 496
`Math_SmoothStepToS` calls are closed. A target-instruction pass also restored
153 complete `AnimationChange` call windows across 48 source units; the 1,541
calls still missing ABI evidence remain an explicit producer worklist rather
than consumer shims.
