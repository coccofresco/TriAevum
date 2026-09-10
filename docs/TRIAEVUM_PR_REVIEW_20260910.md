# Community PR Integration Review

Reviewed against `port/linux-nri` at `9920fc5`, on 2026-09-10. GitHub's live
API lists **six open PRs**. Heads below are pinned: conclusions do not silently
apply to future revisions. This supplements the
[previous review](TRIAEVUM_PR_REVIEW_20260909.md), without replacing its evidence.
PR diffs were compared from their merge bases, then against the current owners;
older branch files must not replace the current Linux/Android implementation.

## Decisions

| PR | Author | Current decision |
| --- | --- | --- |
| [#6 Linux infrastructure](https://github.com/coccofresco/TriAevum/pull/6) | 999sian | Already selectively integrated in `f1c05a8`; do not merge obsolete packaging over current work. |
| [#11 local PICA cache](https://github.com/coccofresco/TriAevum/pull/11) | 999sian | Retain useful persistence/strict-mode ideas, not this implementation wholesale. Forge/NRI preparation now has shared owners. |
| [#13 TopScreen ZL/ZR](https://github.com/coccofresco/TriAevum/pull/13) | 999sian | Concrete product-dispatch fix worth adapting next; input lifetime needs its own tests. Not integrated in this review. |
| [#14 display failure feedback](https://github.com/coccofresco/TriAevum/pull/14) | 999sian | Integrated with corrected failure lifetime and actual-widget regression coverage. |
| [#15 guest thread](https://github.com/coccofresco/TriAevum/pull/15) | 999sian | Defer: exception lifetime defect and no demonstrated stable throughput gain. Useful phase decomposition, not a default optimization yet. |
| [#16 Apple Silicon](https://github.com/coccofresco/TriAevum/pull/16) | PabloVSouza | Valuable experimental port; split portable fixes from Mac host/package support. Not integrated or advertised as supported. |

Pinned heads:

```text
#6  505d7b8c974a2eadd5a362933a5ffdc19bd28214
#11 28fa5fdd60dc79c9145dc299f4440a90a8f1d026
#13 800345379958fc7d9c03555e8ecb28c1f8e44dea
#14 a359f7063a512e7626eed4e5d849dd46aab8fc79
#15 99454b757805c5580a2cada2ac214870fcd52693
#16 49fbfcd7149526d1fc0474055279af9a556b77e6
```

## Findings And Required Adaptations

### #15: Worker Lifetime Before Parallelism

**High, exception path:** in the PR's
`tools/oot3d/native_game_runtime/oot3d_native_a32_window.cpp:6264`, the worker
captures loop-local lambdas and HUD storage by reference. Main calls
`presentHalf(presentInputs)` at line 6272 before `guestWorker.Wait()` at 6277.
If presentation throws, unwinding destroys those frame-local objects while the
worker may still use them. The worker destructor at line 4033 joins too late:
the worker lives outside the loop, and its destructor runs after the frame's
locals have been destroyed. Forwarding worker exceptions does not solve an
exception on the main thread. This is a code-path finding, not a reproduced
hardware crash.

Before adoption: use an exception-safe task scope that waits before captured
objects are destroyed, preferably immutable owned frame packets instead of
references into the large loop. Inject a presentation failure while the worker
is busy and test shutdown, resize, quickload and device-loss paths. Keep the
thread coordinator reusable, with title behavior in the OOT3D adapter.

**Measurement gap:** `renderHud()` at line 6281 precedes the reset of
`phaseStart`; `MainHudAndEndFrameSeconds` at 6295 consequently excludes HUD
rendering despite its name. Packet gathering/flush work is not fully partitioned
either. Fix the accounting before using the spans as a complete CPU budget.

The contributor's 1,200-frame uncapped measurements are 60.8/39.7 FPS inline
versus 63.9/37.0 threaded, with large GPU-wait variance. They explicitly conclude
that the difference is within run-to-run noise. Drain is about 9 ms/frame but
only about 2 ms of rendering overlaps it. A deeper queue may help but adds input
latency and changes ownership; that is not implemented in this PR. No local
performance gain is claimed or inferred from donor measurements.

### #11: Preserve Cache Correctness And Forward Progress

The previous review's findings remain applicable to this unchanged head:

- `gfx_vulkan.cpp:3831`, `ConfigureLocalPicaShaderCache`: descriptor schema is
  insufficient to invalidate stale generated modules. A local source can become
  authoritative under the same guest key after the generator changes.
- `gfx_vulkan_pica.cpp:3809`: `atoi` budget parsing admits zero/malformed values,
  leaving the guest held behind a queue that does not advance; negatives wrap.
- Queued recipes need immutable profile identity, cancellation on changes and
  bounded work. Shutdown-only saves lose discoveries on a crash.

Use the current shared preparation job, NRI pipeline factory and validated
Vulkan device-cache store rather than introducing another coordinator inside
`GfxRenderingAPIVulkan`. Commits `eed1a02` and `9920fc5` already implement and
measure the Forge/device-preparation path on Windows/Linux, with an Android
compile check. See [pipeline preparation](TRIAEVUM_NRI_PIPELINE_PREPARATION.md).
That work is not copied PR #11 code, and does not mean every feature of its
per-user portable recording cache is implemented. Useful remaining pieces are
atomic incremental discovery, strict-mode shutdown handling and explicit
outline-occlusion recipe coverage, each in its existing owner.

### #13: Missing Product Hooks Are Real; Press Lifetime Is Separate

The current `oot3d_native_a32_window.cpp:3675` registers item getters only in the
non-product branch; `ExecuteProductTopScreenCamera` at 1606 handles camera PCs
only. The PR correctly identifies that registering native candidates does not
by itself intercept compiled product code. Its item query/slot override and
Items-page assignment call/return exits are useful. Preserve the
handled-or-resume-once behavior so an unhandled exit cannot spin on the same PC.

Do not conflate that routing fix with the proposed six-refresh press latch
(PR line 5061). At line 1491 consuming a latch does not clear the current
`TopScreenInput` pressed value; a second getter in that refresh can return true
again. That may be valid tick-wide query behavior, but is not an exactly-once
action contract. Put host-edge capture in the common input owner, with explicit
TopScreen consumption/reset behavior. Cover short taps, held buttons, repeated
queries, pause transitions, focus loss and loadstate. Then verify actual item
use and assignment in a product-mode gameplay state.

The author's five controller presses reached the guest once each in the reported
session. That is useful evidence, not coverage of repeated getter calls or every
item override. This review does not claim the current product is fixed.

### #16: Recover Portable Fixes, Avoid A Second Release Pipeline

The contributor reports native Apple Silicon boot/gameplay and a usable local
AppKit/Forge bundle. Preserve authorship by
[Pablo Souza](https://github.com/PabloVSouza); the port is not a generic AI rewrite.
Its 99-file diff includes the already published UI contract recovered from our
`c5d8534` commit. Do not reimport that dependency or overwrite newer files.

- The logical-window/drawable-size swapchain fix is independently already
  covered here by `gfx_vulkan.cpp:2264`: unchanged requests return early and
  actual extents/window events control recreation. Keep the current mobile and
  Wayland-aware implementation.
- The PR's full-drawable readback/resampling fixes a remaining common issue:
  current `ReadFramebufferToCPU` at `gfx_vulkan.cpp:2476` crops to the top-left
  when output pixels exceed requested screenshot dimensions, or pads when
  smaller. Worth a separate portable patch with RGBA/BGRA, equal-size, up/down
  scaling and real HiDPI capture tests. Not changed in this tranche.
- Conditional Vulkan portability extensions and AppleClang/libc++ trait fixes
  belong in common capability/compiler owners. Mac dylib discovery belongs in
  the platform loader, preserving the current Android/Linux branches.
- `macos_forge.py` repeats installation/profile construction;
  `package_macos.py:103` copies development trees and performs signing checks
  but does not run our common release allowlist/catalog audit. Keep AppKit as
  a thin UI and register a Mac target with shared Forge, title compatibility,
  source inventory, shader preparation and packaging contracts. Ad-hoc signing
  is not release-content validation.
- The port disables NRI/CACAO/SSSR. It does not establish Mac parity for the
  default NRI renderer. No Mac device is available for local qualification.
- The pacer change at `oot3d_native_presentation_pacer.cpp:134` reduces recovery
  debt from 250 ms to two periods. Evaluate separately against clock/update
  counts and frame-time distributions; do not merge global pacing policy as a
  Mac build fix or assume a presentation-FPS gain is more game simulation.

### #14: Integrated At The Existing Settings Owner

Integration commit: `dd7539e` (with original-author attribution).

Preserved the contributor's backend reason propagation and F1 display message.
Adaptation: accept a reason only after validating/restoring the current failed
candidate. Idle/stale feedback cannot replace it. A rollback acknowledgement
does not clear the failure; successful application of a new candidate does.
No persistent setting, second status owner or title-specific behavior added.

## Verification And Traceability

- Linux incremental `triaevum_public_runtime` and real
  `triaevum_f1_settings_smoke` build passed; no title recompilation.
- Actual-widget smoke passed **1,797 assertions**, including per-frame invariants,
  backend failure visible from Grass, obsolete/idle responses, rollback,
  retry, successful clearing and restoration of the original display settings.
  These are not 1,797 separate gameplay scenarios.
- Bounded Vulkan native-fidelity run: **900 presentations**, normal exit.
  All six framebuffer captures (120/270/420/570/720/870) are byte-identical to
  the pre-change baseline. Private evidence is under the Linux test workspace's
  `triaevum-pipeline-live-proof/pr14-live`; no ROM or captures are published.
- This is correctness verification, not a throughput benchmark. Windows,
  Android, Mac device behavior and rejected real monitor modes were not tested
  in this tranche. The error path was injected through the actual settings/UI.
- No GitHub PR was merged, closed, commented on or pushed during this review.
  See [contribution record](TRIAEVUM_CONTRIBUTIONS.md). Selective integrations
  carry PR URL, original SHA and the original public Git author trailer.

Next implementation order: #13 product item hooks with input tests; portable
HiDPI readback; remaining #11 recording behind shared cache owners. Revisit #15
only after lifetime repair and a meaningful measured gain. Qualify #16 through
the shared platform pipeline, with the contributor's Mac hardware evidence.
