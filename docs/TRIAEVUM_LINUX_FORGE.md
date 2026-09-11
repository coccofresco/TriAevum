# Linux Forge

Latest development alignment and external Tk packaging repair:
[September 10-11 qualification](TRIAEVUM_LINUX_PARITY_20260911.md).
The host Forge is rebuilt and GUI-tested; the installed Flatpak is not yet
updated to this source baseline.

Status: Linux x86-64 Forge development build, installation and native game boot
tested on the physical Linux host on 2026-09-09. GUI visibility on that host is
still unverified. This is not a public Linux release or Steam Deck qualification.

## Player Contract

The player selects a personal decrypted `.cci` or `.3ds` ROM. Forge extracts
and validates the required inputs, applies the existing revision adapters,
prepares the data index, installs the publisher-built title module and creates
the launch profile. It never compiles or translates game code on this path.
The existing TopScreen texture acquisition, configuration preservation,
save directories and transactional activation remain shared with Windows.

Python, Tk, Capstone and HTTPS root certificates are bundled. Linux Forge is a directory bundle: do not
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
- `native_process.py`: restores the caller's library search path for the native
  runtime and ABI probes. Forge workers keep the frozen Python environment.
  Otherwise PyInstaller's `LD_LIBRARY_PATH` can inject bundled Python/Tk
  dependencies into the game. This follows the
  [PyInstaller native-child guidance](https://pyinstaller.org/en/stable/common-issues-and-pitfalls.html#launching-external-programs-from-the-frozen-application).
- `https_transport.py`: preserves system TLS trust and adds bundled
  [certifi roots](https://github.com/certifi/python-certifi) in frozen builds.
  The first physical-host install exposed missing build-host OpenSSL CA paths.
  Certificate and hostname verification remain enabled; no insecure retry.
- The Linux runtime uses origin-relative RUNPATH for its bootstrap library.
  The relocated test package resolves that library inside the package, not
  the publisher's build directory. The fix required only an incremental link
  (2.2 seconds), not rebuilding the translated game.

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
triaevum_title_aot.so                  # empty runtime bootstrap dependency
forge/oot3d_game_module.so
titles/<execution-id>/triaevum_title_aot.so  # actual translated game
recipes/precompiled-titles.json
recipes/oot3d.json
source/titles/...-translated.zip
config/...
resources/...
recipes/adapters/...
LICENSES/...
```

The example omits detailed resource/dependency files; use the same validated
recipes, adapters, configuration and source artifacts as the reference package.
Do not copy DLLs as Linux modules. The title must be the real build documented
in [the Linux port report](TRIAEVUM_LINUX_PORT.md), not the runtime's empty stub.
Both `.so` roles are required: the root bootstrap satisfies an ELF dependency;
Forge selects the separately catalogued real title through an immutable private
plugin path. Do not overwrite one with the other.

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

## Repeat the Full Installation Test

These developer helpers stage a **private test candidate**, not an audited
public release. The reference directory supplies only explicitly selected
recipes, resources, translated sources and notices. No Windows DLLs, personal
data or existing public release manifest are reused as Linux artifacts.

```sh
python3 tools/triaevum_release/stage_linux_forge_candidate.py \
  --reference /path/to/reference-package \
  --forge-bundle "$HOME/triaevum-forge-dist/TriAevumForge" \
  --runtime-build "$HOME/triaevum-linux-build" \
  --title "$HOME/triaevum-linux-title-build/triaevum_title_aot.so" \
  --output "$HOME/TriAevum Linux Test" \
  --source-commit FULL_PLATFORM_GIT_COMMIT
python3 tools/triaevum_release/qualify_linux_forge.py \
  --installation "$HOME/TriAevum Linux Test" \
  --rom /path/to/personal-decrypted.cci \
  --output "$HOME/triaevum-forge-qualification" --seconds 60
```

Use new output directories. Qualification refuses an already active personal
installation. It imports display routing from the logged-in desktop, invokes
the actual frozen GUI installation worker, repeats installation while checking
configuration/save-sentinel hashes, and boots the exact generated game profile.
Every test process is bounded and reaped. Framebuffer readbacks are retained for
inspection; their presence alone is not an automatic image-quality test.

Read `qualification.json`, not only the command exit code. The helper permits
continuing the functional test when a successful install has an iconified GUI,
but reports `functional_pass_gui_visibility_pending`, never a visual pass.
Other installation/layout failures abort. This exception is not a release gate
bypass: public usability must still be verified.

## Evidence (2026-09-09)

- Windows: 68 targeted tests pass; actual source GUI smoke passes.
- Linux: 68 tests run, 67 pass, one Windows-only descendant Job Object test
  skipped. Linux GUI-owner termination is tested, not skipped.
- Build host: Ubuntu 24.04 WSL/WSLg, Python 3.12.3, PyInstaller 6.16.0. Its
  frozen GUI probe passes with mapped controls and no clipping. It does not
  establish physical-GPU performance or visibility on every desktop.
- Physical host: CachyOS/KDE Wayland, RTX 4060, same native runtime/title as
  the port report. Fresh personal EUR0 `.cci` installation into a path with
  spaces completed in **25.55 seconds**, including the official TopScreen
  download. Reinstallation completed in **7.62 seconds**, preserving the
  existing two configuration files and a save-directory sentinel byte-for-byte.
  These are worker/probe elapsed times; full process wall times were 27.86 and
  9.89 seconds. No compiler, SDK acquisition or translation was invoked.
- The installed native game completed its bounded 60-second run, exit 0.
  Framebuffer captures at frames 120 and 720 show the rendered title/scene;
  the latter was visually inspected with grass, toon and the animated logo
  present. TopScreen and the generated default launch profile were retained.
  This does not certify interactive gameplay, audio listening quality, SSSR,
  absence of intermittent flashing, or native/interpolated FPS targets.
- Missing-runtime worker test: exits 1 with a structured error before ROM
  extraction or activation. No compilation fallback or background game.

**GUI issue resolved (same day):** the physical output was powered off by DPMS,
despite KDE being active and unlocked. The failure also affected a minimal Tk
window; a later game run blocked in X11 event waiting before producing frames.
After explicitly waking the output with `kscreen-doctor --dpms on`, the same
frozen Forge passed its real-widget probe inside Steam Runtime 4: normal
760x443 window, no unmapped/clipped controls, correct button enablement.
No Forge rebuild, desktop security change, or window-manager preference change
was necessary. The earlier installation/preservation proof remains separate.

`linux_desktop.py` now checks the optional KDE display-power query before
developer GUI qualification. It rejects known all-off outputs, does not assume
other desktops are broken when the query is unavailable, and never turns on a
screen automatically. `run-linux-title.sh` shares this check. For remote tests,
wake the display first and keep it active; an unlocked login alone is insufficient.
The post-wake report is privately retained as
`I:/oot3dre_work/linux-port-proof/forge-steamrt4-display-on.json`.

Private evidence: `I:/oot3dre_work/linux-port-proof/forge/qualification-4/`
contains `qualification.json`, separate install/reinstall reports, game log,
runtime report and framebuffer captures. The physical candidate is
`~/triaevum-forge-proof/TriAevum Linux Candidate 4`. No test process was left
running. These files and personal ROM-derived data must stay outside Git.

The test uses base commit `80a944864da74bdf4bb726f29799cc5f6dad49cd` plus the
loader/TLS/probe changes accompanying this report. Remote source was updated
by file transfer, not by changing its Git HEAD. The recorded base commit alone
does not certify the complete tested source: a public build needs a fresh,
exact source/artifact binding. The actual title `.so` remains unchanged, hash
`b305d574f815f365d7a73a582c020cd435f7ab2741accfa9638dced833a2097c`.

## Before a Player Release

1. Physical desktop widget visibility now passes. Verify end-user file selection
   and interaction again on the final bundle and Steam Deck; do not substitute
   the earlier iconified worker result for the new visible-GUI evidence.
2. Stage native dependencies/resources, adapt the release allowlist/audit to
   Linux and include corresponding source and all bundled-library licenses.
   Current Windows packaging/auditing must not be bypassed or relabeled.
3. The runtime/title [Steam SDK build](TRIAEVUM_STEAM_RUNTIME_BUILD.md) and a
   native Steam Runtime 4 GPU boot now pass. Complete portable packaging and
   test on Steam Deck Desktop Mode; these desktop checks are not Deck coverage.
4. Repeat the now-passing installation, preservation and TopScreen tests on the
   final portable bundle; verify visible GUI and launch in Steam/Game Mode.
   Keep Windows tests as a regression check. Performance and SSSR remain
   separate renderer work, not claimed fixed by this installer change.
