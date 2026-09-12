# Fullscreen lifecycle regression

## Reproduction and cause (2026-09-12)

The distributed alpha.2b Windows runtime exits with code 1 when minimized
from borderless mode with F1 open. The failing run ended at 12.83 seconds,
before any scripted mode transaction. Its error was:

```text
native PICA composition sequence published outside an active Vulkan frame
```

`StartFrame()` can legitimately return without acquiring a swapchain image
when the surface is minimized, unavailable, or out of date. The host nevertheless
advanced the guest and submitted its composition. This is a host/backend
lifecycle error, not a PICA asset or title-logic error. It reproduces a concrete
fullscreen-related crash; it does not establish that every user report has the
same cause.

## Fix and ownership

- The shared rendering API exposes `HasActiveFrame()`. Vulkan reports its actual
  acquisition state; existing synchronous backends retain their previous contract.
- The product window host restores the presentation scheduler snapshot when no
  frame was acquired, balances GUI/window calls, yields briefly, resets pacing,
  and processes window events again. No guest time or PICA submission is consumed.
- The module host observes the same contract before advancing module time.
- Out-of-date/not-ready acquisition returns to the host instead of retrying
  indefinitely without pumping events. Actual GPU failures remain errors.
- NRI callback errors also reach stderr for user launch logs.

No canonical shaders, composition order, assets, UI layout, or AOT title module
were changed. The module-host guard is source-level coverage, not a claim of a
separate end-to-end module-host run.

## Automated verification

Run from the repository root against a prepared private installation:

```sh
python -m tools.triaevum_release.probe_display INSTALLATION EXECUTABLE OUTPUT --case modes --seconds 75 --validation
python -m tools.triaevum_release.probe_display INSTALLATION EXECUTABLE OUTPUT --case rapid --seconds 75 --validation
python -m tools.triaevum_release.probe_display INSTALLATION EXECUTABLE OUTPUT --case recovery --seconds 75 --validation
python -m unittest tools.triaevum_release.tests.test_display_probe
cmake --build BUILD --target triaevum_presentation_clock_tests
```

Then run `triaevum_presentation_clock_tests` from BUILD. The clock regression
checks that 120 suspended attempts preserve guest time and interpolation phase.
It is independent of the legacy aggregate runtime test target, which currently
cannot link without its optional cutscene-host dependencies.

The display probe uses the same settings transaction owner as F1. It checks
actual modes, applied transitions, rollback, sustained rendering and GPU errors,
not merely process exit. Missing diagnostic hooks and truncated GPU history fail
verification. It captures framebuffer images through the runtime. These are not
performance benchmarks. The injected-failure case exercises recovery without
requiring a real GPU failure.

Supplement with real window actions: start borderless, open F1, minimize/restore
twice after the intro is running, then repeat F11 toggles. The diagnostic sequence
alone does not generate operating-system minimization or prove widget hit testing.
An unusual exclusive resolution may legitimately select the closest display mode.

## Recorded results

| Scenario | Result |
| --- | --- |
| Alpha.2b Windows minimize with F1 | Reproduced fatal inactive-frame submission |
| Corrected Windows minimize/restore twice | Exit 0; 1,028 frames on final functional build |
| Corrected Windows mode/resolution/confirmation matrix | Exit 0; 2,580 frames; modes 0/1/2; 8 applies, 1 rollback, 0 apply failures; GPU validation errors 0 |
| Windows 24 rapid requests, acquired-frame guard build | Exit 0; 2,000 frames; 16 applies, 8 rollbacks; GPU validation errors 0 |
| Windows injected apply failure, baseline | Exit 0; 6 failures safely rolled back; windowed retained |
| Windows F1 actual-widget smoke | 3,896 assertions passed against updated runtime |
| Python probe verifier | 5 tests passed |
| Presentation clock test | Passed |
| Linux fixed minimize/restore twice, XWayland | Exit 0; all 4 window actions recorded |
| Linux fixed mode matrix | Exit 0; 1,912 frames; modes 0/1/2; 8 applies, 1 rollback, 0 apply failures; reported GPU errors 0 |

Linux runtime compilation passed with both the guard and bounded-acquisition
changes. Linux execution above covers the guard build; the final bounded-retry
binary has not yet been re-executed because permission review blocked updating
the private test copy. Windows final matrix/minimize cover both changes.
Linux matrix did not request Vulkan validation layers. No physical Steam Deck,
multi-monitor migration, hot unplug, or device-loss recovery is claimed.

NRI may log recovered `VK_ERROR_OUT_OF_DATE_KHR` (-1000001004) at ERROR severity
when presenting during minimization. This was observed and is not a zero-error
validation run: the backend marks the swapchain dirty and resumes. Do not hide
these messages or misreport them as shader/descriptor validation failures.

Private evidence roots: Windows `%TEMP%/TriAevum-fullscreen-20260912`; Linux
`~/triaevum-fullscreen-20260912`. The Linux `FixedMatrix` log/runtime/GPU files
were also checked locally with the public probe verifier. Captures, game data,
saves and test installations must not enter source or public packages.

## Release boundary

The source fix does not update the already published alpha.2b artifacts.
Before shipping, rebuild and bind the executable to its package catalogue,
include corresponding source, run the release audit and packaged-launch checks.
The private Linux test copy has a replaced binary and is not a releasable package.
