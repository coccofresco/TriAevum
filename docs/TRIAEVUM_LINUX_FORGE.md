# Linux Forge

Status: Linux x86-64 Forge development build, tested on 2026-09-08. This is
not yet a complete Linux game release or Steam Deck qualification.

## Player Contract

The player selects a personal decrypted `.cci` or `.3ds` ROM. Forge extracts
and validates the required inputs, applies the existing revision adapters,
prepares the data index, installs the publisher-built title module and creates
the launch profile. It never compiles or translates game code on this path.
The existing TopScreen texture acquisition, configuration preservation,
save directories and transactional activation remain shared with Windows.

Python, Tk and Capstone are bundled. Linux Forge is a directory bundle: do not
distribute only its executable or omit `_internal`. Vulkan drivers and the
native game's shared-library dependencies are a separate packaging concern.

## Implementation

- `tools/triaevum_release/release_platform.py`: one platform boundary for
  executable/module filenames and target triples. Windows catalogs without a
  target keep their legacy meaning; Linux catalogs explicitly declare
  `x86_64-unknown-linux-gnu`. Foreign-platform installs are rejected.
- `precompiled_titles.py`, `forge.py`, `installed_runtime.py`: ELF title
  selection, immutable `.so` publication and platform-aware launch receipts.
  Input identities, hash checks and save/configuration rules are unchanged.
- `build_forge_binary.py`: Linux defaults to PyInstaller `onedir`; Windows
  keeps `onefile`. No temporary interpreter extraction at every Linux launch.
- `forge_gui.py`: native Tk metrics determine the minimum window height.
  The first real Linux test caught both action buttons below the fixed-height
  window; the corrected layout is 760x437 on the test theme.
- `worker_job.py`: a Linux pidfd terminates an installation worker when its
  owning GUI exits. Windows retains its Job Object implementation. Runtime
  ABI probes remain bounded to 15 seconds; no long-running compiler children
  exist in the player install path.
- `linux_precompiled_catalog.py`: publisher-only catalog binding to Linux
  ELF64 x86-64 binaries. Preserves ROM families, adapters and translated-source
  provenance; verifies reference source bindings and native title ABI before
  replacing the catalog. It does not compile, stage files or certify gameplay.

## Build and Repeat Tests

Run in the source root with native Linux Python and Tk installed. On the
Ubuntu test environment, publisher prerequisites were `python3-tk` and
`python3-venv`; users of a packaged release do not install these.

```sh
python3 -m venv "$HOME/.venvs/triaevum-forge"
PY="$HOME/.venvs/triaevum-forge/bin/python"
"$PY" -m pip install -r tools/triaevum_release/requirements-forge-build.txt
"$PY" tools/triaevum_release/test_forge_port.py
"$PY" tools/triaevum_release/build_forge_binary.py \
  --output "$HOME/triaevum-forge-dist" \
  --work "$HOME/triaevum-forge-build" \
  --nlohmann-include /path/to/include
FORGE="$HOME/triaevum-forge-dist/TriAevumForge/TriAevumForge"
"$FORGE" doctor --inventory
timeout 15 "$FORGE" --gui-smoke --output /tmp/triaevum-forge-gui.json
```

The include path must contain `nlohmann/json_fwd.hpp`. It is a publisher input
for existing bundled developer tools, not an end-user compiler requirement.
`doctor --inventory` checks bundled components only; it is not a game test.
The GUI probe opens the actual window, checks clipping and action-button
enablement and closes itself. Run it inside a graphical session.

## Package Assembly

Merge the entire Forge output directory into the staged game package root:

```text
TriAevum
TriAevumForge
_internal/...
forge/oot3d_game_module.so
titles/<execution-id>/triaevum_title_aot.so
recipes/precompiled-titles.json
recipes/oot3d.json
source/titles/...-translated.zip
config/...
resources/...
recipes/adapters/...
licenses/...
```

The example omits detailed resource/dependency files; use the same validated
recipes, adapters, configuration and source artifacts as the reference package.
Do not copy DLLs as Linux modules. The title must be the real build documented
in [the Linux port report](TRIAEVUM_LINUX_PORT.md), not the runtime's empty stub.

After staging actual Linux binaries and reference supporting files:

```sh
python3 tools/triaevum_release/linux_precompiled_catalog.py \
  --reference-package /path/to/reference-windows-package \
  --installation /path/to/linux-package \
  --plugin /path/to/linux-package/titles/execution-id/triaevum_title_aot.so \
  --source-commit FULL_PLATFORM_GIT_COMMIT
```

This operation accepts one shared translated execution image. Different ROM
recipes can reference it only when their bound source/execution identities
match; input adapters remain responsible for their documented differences.
The publisher must build the `.so` from those sources: an ABI check alone does
not prove translation equivalence. Existing `linux_title` CMake validates
the translated source inventory before building it.

## Evidence and Remaining Work

- Windows: 57 targeted tests pass; actual source GUI smoke passes.
- Linux: 57 tests run, 56 pass, one Windows-only descendant Job Object test
  skipped. Linux GUI-owner termination is tested, not skipped.
- Frozen Linux binary: doctor passes; real GUI probe passes with no clipped
  controls and correct Prepare button enablement.
- Frozen Linux binary: real EUR0 extracted ROM data verified with fresh hashes
  and prepared into `content.tap` plus `process-manifest.json`, exit 0. This is
  data preparation only, not module installation or a game boot.
- Missing-runtime worker test: exits 1 with a structured error before ROM
  extraction or activation. No compilation fallback or background game.
- Build host: existing Ubuntu 24.04 WSL/WSLg. Forge is a native Linux ELF with
  its own interpreter. This GUI result says nothing about GPU performance.

Private evidence is under `I:/oot3dre_work/linux-port-proof/`, including
`forge-gui-smoke-fixed.json`, `forge-windows-gui-smoke.json` and
`forge-native-prepared/`. The local candidate bundle is under
`I:/oot3dre_work/linux-port-proof/forge/`. No private game data belongs in Git
or the Forge artifact.

Still required before a player release:

1. Full GUI ROM-to-installed-game run against the actual Linux runtime and
   title. The existing CachyOS test host stopped responding on SSH during this
   work; its successful prior game boot is not a Forge installation test.
2. Stage native dependencies/resources, adapt the release allowlist/audit to
   Linux and include corresponding source and all bundled-library licenses.
   Current Windows packaging/auditing must not be bypassed or relabeled.
3. Build against the selected Steam-compatible libc baseline and test in a
   clean environment, including Steam Deck Desktop Mode. The Ubuntu 24.04
   development build is not a portability guarantee.
4. Verify installation into paths with spaces, reinstallation preserving saves
   and settings, TopScreen acquisition and launch in Steam/Game Mode on the
   actual release bundle. Keep tests on Windows as a regression check.
