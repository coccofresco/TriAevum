# PR Review: 2026-09-09

Reviewed against local `port/linux-nri` at `26e7247`. New PRs are by
[999sian](https://github.com/999sian). This is a code review plus a separate
shader-import implementation, **not a merge or gameplay qualification of the
PRs**. Do not close their GitHub PRs as merged based on this document.

## PR #11: Shader Cache

[PR](https://github.com/coccofresco/TriAevum/pull/11),
head `28fa5fdd60dc79c9145dc299f4440a90a8f1d026`.
Recommendation: adopt the persistence/prewarm approach, with changes before
default enablement. It is not a Citra `.bin` importer and does not prepare the
first installation in Forge.

### Findings

1. **High: old local shaders can become authoritative after an upgrade.**
   `ConfigureLocalPicaShaderCache` compares descriptor schemas only against an
   explicitly supplied pack, and otherwise accepts the local pack/manifest as
   their own reference. There is no producer/generator revision. Prewarm inserts
   the old sources into `mCanonicalNativePicaShaders` by guest shader keys;
   `DrawNativePica` reuses a found shader without checking the freshly generated
   source identity. A generator correction not changing the guest-state key can
   therefore be hidden by yesterday's cache. Bind caches to the actual compiler
   and generator contract and verify effective source identity on reuse.

2. **High for invalid configuration: prewarm can block forever.**
   `PrewarmNativePicaPipelines` parses `OOT3D_PICA_PIPELINE_PREWARM_BUDGET_MS`
   with `atoi`, then only processes work before the deadline. Zero or malformed
   input gives a zero-length batch; the queue never advances while the host
   deliberately holds the guest. Negative values wrap to a huge unsigned
   budget. Validate/clamp the value and guarantee progress of at least one
   item per eligible batch, plus cancellation and explicit failure state.

3. **Medium: queued work is not bound to the profile used to execute it.**
   Entries are filtered on enqueue but an unfinished queue survives another
   profile being enqueued. `PrewarmNativePicaPipelineEntry` assumes the earlier
   match still holds, while pipeline creation uses current sample/attachment
   requirements. Bind batches to an immutable profile identity; invalidate or
   reschedule on profile changes. Test F2/MSAA changes during a long batch.

4. **Recovery/architecture gaps:** persistence is shutdown-only; a crash loses
   newly discovered entries. Cache lifetime and queue logic add substantial
   state to the Vulkan class. Extract a reusable store/coordinator, keep GPU
   execution in NRI, and checkpoint atomically at safe boundaries. Corrupt local
   caches should cause a bounded rebuild, never loss of native draws.

Useful retained ideas: separate portable modules and pipeline manifests,
profile-filtered prewarm, strict-pack validation not satisfied by local hits,
and explicit outline-occlusion manifest coverage. The contributor reports warm
session improvements on Intel; those are donor results, not our Android or
Windows measurements. Our broader Forge/Citra integration and measured results
are in [shader preparation](TRIAEVUM_FORGE_SHADER_PREPARATION.md).

## PR #13: TopScreen ZL/ZR

[PR](https://github.com/coccofresco/TriAevum/pull/13),
head `800345379958fc7d9c03555e8ecb28c1f8e44dea` (two commits).
Recommendation: the missing product-mode observable exits are a concrete
integration defect and the handled-or-resume-once route is appropriate. Retain
the item getter/assignment routing, but qualify the input latch separately.

- Product builds currently retain camera exits but omit the item getters and
  slot resolver. Registering native candidates alone does not intercept compiled
  code. The PR also handles the Items-page assign call/return path.
- The latch clears `TopScreenZrPressLatched`/`TopScreenZlPressLatched`, but leaves
  the current `TopScreenInput.*Pressed` snapshot true until the next refresh.
  Thus the code does not enforce the comment's exactly-once claim if the same
  getter is observed twice in one refresh. Decide whether the contract is
  native tick-wide pressed state or consuming action, and test that exact rule.
- A six-refresh expiry is input UX policy, not an OOT3D format value. Test taps
  during pause/cutscene transitions, focus loss, held inputs, both triggers,
  assignments and repeated getter calls. Keep edge capture in the shared input
  layer and TopScreen semantic consumption in its adapter, instead of adding
  another set of unrelated booleans to the host loop.
- The contributor reports Linux controller/timeline tests; our review did not
  repeat those gameplay tests. No claim that all item overrides are qualified.

## PR #14: Display Rejection Feedback

[PR](https://github.com/coccofresco/TriAevum/pull/14),
head `a359f7063a512e7626eed4e5d849dd46aab8fc79`.
Recommendation: small, useful change at the correct settings/UI owners;
integrate after adding state-machine tests.

`RejectPresentationApply` writes the displayed reason before checking whether
the rejected request is still current. Move that assignment after successful
request validation so stale backend feedback cannot overwrite current status.
Also test that restoring the previous display mode does not clear the failure
reason as if a new user-selected mode had succeeded. The existing generic
`AcknowledgePresentationApplied` clears on any acknowledgement, including a
rollback phase. These are lifecycle concerns, not a request for another menu.

## Existing PR #6

The Linux infrastructure PR is still open. It was already selectively adapted;
do not re-merge its older platform/packaging implementation over the current
Flatpak and Android work. See [contribution history](TRIAEVUM_CONTRIBUTIONS.md).

## Attribution And Integration Order

1. Retain PR #14's original author when applying the small UI change and fixes.
2. Integrate PR #13's product dispatch route with focused input tests.
3. Adapt PR #11 into the two-level preparation architecture, without replacing
   the accepted Android surface/pipeline lifetime work.
4. Preserve original commits or accurate coauthor trailers for copied code and
   update the contribution record with the actual adopted scope. The new Citra
   parser/Forge preparation stage in this tranche is not code from PR #11.

No PR was merged, publicly reviewed, closed, or pushed during this review.
