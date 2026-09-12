# Shader Distribution And Guest-Thread Review

2026-09-10. Baseline `ac77f6c`, `port/linux-nri`. No GitHub release or PR merge
is performed by this work. PR contributions retain their attribution in
[the contribution register](TRIAEVUM_CONTRIBUTIONS.md).

## Shipped Preparation Contract

The Windows publisher now builds and includes the renderer shader compiler and
its matching shaderc DLL, and binds their sizes/hashes to every title revision.
`renderer_shader_preparation` is independent of the optional game-specific
`shader_preparation` seed. The former uses only renderer-owned source generators;
the latter retains the existing private seed/preparation/cache path unchanged.
Forge prepares the pass cache before activation and routes that same writable
installation cache into the game. No title/AOT recompilation is involved.

Owners:

- `tools/triaevum_release/shader_release_layout.py`: common publisher binding,
  no private shader promotion or hardware-specific cache redistribution.
- `prepare_release.py`: Windows default inclusion, not an opt-in developer flag.
- `linux_precompiled_catalog.py` and `stage_linux_forge_candidate.py`: explicit
  Linux helper/dependency binding; refuse to inherit a Windows renderer helper.
- `precompiled_titles.py`: verify all bound renderer artifacts during audit and
  selection, before running them. Old catalogs without preparation still work.
- `shader_preparation.py`: standalone renderer contract, legacy combined
  contract compatibility, repair/recheck and existing atomic cache storage.
- `native_process.py`: helper-only sibling library lookup on Linux, after
  stripping PyInstaller loader pollution. Indirect shaderc dependencies are
  covered; ordinary game launch policy is unchanged.
- `flatpak_package.py`: preserve executable mode on preparation helpers.
- `release_policy.json`: distinct shader-tool role, not permission to ship a
  C++ toolchain/SDK, private shader packs or driver caches.

Linux staging example (helpers must be built for the destination runtime):

```text
python tools/triaevum_release/stage_linux_forge_candidate.py
  --reference WINDOWS_REFERENCE --forge-bundle LINUX_FORGE
  --runtime-build LINUX_BUILD --title LINUX_TITLE --output NEW_CANDIDATE
  --source-commit FULL_COMMIT
  --shader-compiler LINUX_BUILD/oot3d_native_pica_aot_compiler
  --shader-dependency MATCHING_LIBRARIES/libshaderc_shared.so.1
```

Repeat `--shader-dependency` for dynamic glslang/SPIRV-Tools dependencies if
required by that build. Use regular, explicitly staged SONAME files. Do not
copy a distribution's glibc or Vulkan driver into the package. Flatpak helpers
must be built/qualified against the selected Flatpak/Steam runtime, not assumed
portable because they work on the Linux development host.

## Windows Cache Identity Defect

The first relocated helper test passed (22 cold compiles, then 22 hits), but
the real game still compiled 21 pass shaders. This was not preparation success.

`renderer/shaderc_compiler.cpp` took the address of `shaderc_compile_into_spv`
without the shared-library import declaration. On Windows that identified the
EXE's import thunk, adding the helper/game executable fingerprint to the shaderc
contract. Equal shaders and equal compiler DLLs consequently had different
keys in different executables, also invalidating cache on unrelated relinks.

Declare the existing Windows shared-library dependency before including
shaderc's header. The address now belongs to the actual DLL. Compiler/source/
macro/options validation is preserved, not weakened or replaced by filenames.

The integration regression changes the helper executable's byte identity with
a harmless trailing overlay, updates its catalog checksum, and requires reuse
of all entries with unchanged shaderc. Old executable-bound entries are ignored;
no user caches are deleted. A runtime rebuild changed two compiler-wrapper
objects plus normal link/product metadata, not the title DLL.

## Verification

- Windows real helper from a relocated directory with spaces: 22 modules
  compiled/written, then 22 hits and zero compilations, including the changed
  executable-identity regression.
- Fresh public-tool preparation took 3.330 seconds inside shader compilation
  on this PC. This is not total ROM import/package creation time.
- Real Windows NRI game, existing Hyrule Field checkpoint, 240 presentations:
  `PASS_SHADER_CACHE requests=21 hits=21 compiled=0`, exit 0. Before the fix:
  `requests=21 hits=0 compiled=21`, about 3.38 seconds of redundant compilation.
- The same run still compiled four PICA/scanout modules with the explicitly
  supplied incomplete 815-entry private pack. This is **not** whole-renderer
  zero-compilation qualification. The complete private-corpus results in the
  [earlier handoff](TRIAEVUM_FORGE_SHADER_HANDOFF.md) are separate.
- Linux real relocated helper: 22 cold compiles then 22 hits. This used the
  development host's installed shaderc, not a clean Flatpak or Steam Deck.
- Linux Flatpak staging tests: five passed, including actual helper execute
  permission; no new Flatpak install is claimed.
- Focused Windows packaging, integrity, preparation and probe suite: 87 tests,
  84 passed and three optional/platform skips;
  optional real compiler tests are run explicitly, not counted as skipped proof.

Private evidence: `J:/TriAevum-verify-20260910/public-shader-tools-{proof,fixed}`,
`public-shader-game-{proof,fixed}`, and
`/home/xander/triaevum-shader-distribution-review/`.

Remaining distribution work: a permitted public game seed or ROM-derived seed
recipe, complete packaged Flatpak helper dependency qualification, and Android's
in-process installer adapter. This change closes default Windows distribution
of the renderer-owned pass preparation, not all three platforms or all PICA
programs. No release readiness claim is promoted automatically.

## PR #15 Admission Result

Reviewed contribution: [999sian's PR #15](https://github.com/coccofresco/TriAevum/pull/15),
head `99454b757805c5580a2cada2ac214870fcd52693`. No worker code is imported into
the production runtime.

`qualify_pr15_lifetime.py` extracts the exact worker from that pinned Git blob,
compiles a bounded standalone fixture, and injects presentation failure while
the worker is pending. A destruction marker is kept alive outside the scope;
the fixture deliberately avoids dereferencing freed memory.

Linux result, 100 iterations: the original ordering allows the worker to see
an expired frame **100/100**; joining before frame destruction keeps it alive
**100/100**. This establishes the ordering defect and a minimal guard's effect.
It does not establish absence of data races, renderer thread safety, device-loss
recovery or gameplay equivalence. The guard is a fixture, not a product fix.

### Bounded Performance Baseline

`probe_renderer.py --throughput` uses the existing native throughput mode. It
requires native timing, no captures and a positive frame bound; warmup defaults
to 120 frames. Runtime diagnostics must confirm VSync, SDL limiting and pacing
are off. Fixed simulation delta prevents duplicate/interpolated presentations
from inflating the work count. Saves/configuration are isolated, and every run
has an external timeout. Future invocations also record the executable hash.

Windows RTX 3060, same Hyrule Field checkpoint and warm application cache:
three 720-step runs, 120 excluded plus 600 measured, no interpolation/effects,
audio retained, no screenshots. Native complete-step throughput:
**59.95 / 59.73 / 60.04 per second**, all exit 0 with all limiters off.
This is a synthetic uncapped throughput test, not normal-speed gameplay FPS.

Whole-run visual presentation totals were 1.29 / 1.29 / 1.38 seconds, whereas
guest totals were 4.86 / 4.91 / 5.09 seconds and renderer frame-start waits
4.60 / 4.54 / 4.15 seconds. These phase totals include warmup and have nesting;
**do not sum them or divide by the warmup-excluded interval**. They indicate
that the approximately 1.8-1.9 ms/step visual section targeted for overlap is
only part of the current cost. They do not predict a measured threaded gain.

Decision: **do not merge or enable #15 as a performance fix yet**. We now have
a reproducible baseline and a reproduced safety defect. A true serial/threaded
A/B remains unperformed; presenting these baseline numbers as its gain would
be false. A follow-up experiment must use scoped/owned work, include all HUD
time in disjoint spans, and pass exception/quickload/resize/shutdown tests before
the same native-step benchmark. Avoid a frame-ahead queue in that experiment:
it changes input latency and would test a different proposal.

Reproduce admission without a renderer rebuild:

```text
python tools/triaevum_release/qualify_pr15_lifetime.py
  --repository REPO_CONTAINING_PINNED_PR --compiler c++ --output NEW_DIRECTORY
```

Alternatively `--source PINNED_WINDOW_CPP` accepts only the exact reviewed Git
blob. Private baseline evidence: `J:/TriAevum-verify-20260910/pr15-field-a{1,2,3}`.
