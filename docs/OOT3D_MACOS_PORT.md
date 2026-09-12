# Experimental macOS port

## Alpha 2 update (2026-09-12)

The Mac branch incorporates upstream release tag `v0.6.0-alpha.2`
(`9587e59`), including the shared grass optimizations, TopScreen/control fixes
and shader-cache refactoring. Upstream's revised Retina resize, scaled
framebuffer copy and `BitFieldUnsigned` implementations replace the original
Mac patches. Mac-specific dylib discovery, the two-period pacing bound and
nonblocking grass admission remain.

The title dylib is rebuilt from alpha 2's translated C++ archive, with each
source hash checked against its manifest. The app bundles the release's 1,039
portable shader modules and the native renderer-pass shader compiler. Forge
prepares these shaders into installation-private storage and detects the ROM's
available languages. Existing installs adopt the bundled portable shader pack
when launched through Play; learned device pipelines remain local. NRI-only
headless GPU pipeline preparation is not enabled on this Vulkan/MoltenVK build.
Missing shader variants are still compiled and cached during play.

The published alpha 2 Windows bundle only lists EUR recipes. The Mac input
preparation step retains alpha 1c's verified USA adapter and content-family
recipe, so the existing USA ROM remains supported with the new native title.
It does not reuse the Windows title DLL or old translated game code.

Build the complete local app with Xcode Command Line Tools, Homebrew and
Python 3.10 or newer:

```sh
brew install cmake ninja sdl2 glew spdlog nlohmann-json tinyxml2 libzip \
  libogg libvorbis opusfile vulkan-headers vulkan-loader molten-vk shaderc python
scripts/macos/build-app.sh
```

The script downloads and verifies pinned official release archives, compiles
the runtime and title, freezes Forge, and packages `build-macos/TriAevum.app`.
It refuses to replace an existing app; provide a different output when updating:

```sh
scripts/macos/build-app.sh "$PWD/build-macos" "$PWD/build-macos/TriAevum-new.app"
```

`TRIAEVUM_PYTHON` can select another suitable Python interpreter. Existing
user saves/settings remain under Application Support and are preserved.
Old graphics configurations retain their choices; fresh installs use alpha 2
defaults. The historical measurements below refer to the alpha 1c Mac port.

Alpha 2 validation on this Mac: runtime/title builds and ABI preflight pass;
frozen Forge imports the USA ROM, prepares the 1,039-module seed and renderer
pass shaders, and preserves existing save files/configuration on repeat import.
ROM language preferences are written beside the Mac runtime configuration,
where the runtime discovers them. File selection and the new-game intro inside
Link's house render successfully. The Shadow2D GPU probe, frame-rate/scheduler
checks, all 12 grass cache tests and the product/source/shader/language Python
checks pass (46 Python tests passed, four platform-specific tests skipped).
The 45-second real-time intro averaged 55.8 FPS; cold shader encounters caused
a maximum interval of 820 ms. This is not a full-game compatibility or steady
60 FPS claim. Native title execution reported zero memory faults, unsupported
exits or retained ARM fallbacks in the exercised sequences.

Status: the Apple Silicon runtime and generated title plugin build natively,
and the local `build-macos/TriAevum.app` imports the user's decrypted USA ROM,
boots the game and reaches playable 3D scenes through MoltenVK. The AppKit
launcher, frozen Forge importer, title dylib and runtime dependencies are bundled.
This is an experimental local build, not a notarized release or a full-game
compatibility claim. Work is on `port/macos-arm64`.

## macOS continuous integration

The `TriAevum macOS` workflow uses the pinned `macos-26` Apple Silicon runner.
Relevant pull requests and pushes build the native runtime, shader compiler and
AppKit launcher, validate Vulkan product information, and run the three public UI
tests plus 14 pacing, grass-cache and framebuffer-readback checks. These checks
need no ROM or GPU gameplay session. Other release-policy tests retain their
Windows/Linux platform coverage.

For a complete development app, open **Actions → TriAevum macOS → Run workflow**,
select the Mac branch and leave **build_app** enabled. This manual run also builds
the hash-verified alpha 2 title, freezes Forge and packages the app. Verification
copies the app to a path containing spaces, checks architecture, dependency
relocation and signing, loads the title ABI, and runs the frozen importer's help
command without a ROM. Download the `TriAevum-macos-arm64-<commit>` artifact for
the zipped `.app`; it is ad-hoc signed, not notarized. The workflow must exist on
the repository's default branch before GitHub exposes the manual Run button.

Run the isolated contract checks locally with:

```sh
cmake -S tools/triaevum_release/macos_checks -B build-macos/contract-tests -G Ninja
cmake --build build-macos/contract-tests
ctest --test-dir build-macos/contract-tests --output-on-failure
python3 tools/triaevum_release/verify_macos_app.py build-macos/TriAevum.app \
  --output build-macos/app-verification.json
```

Hosted CI does not run the Shadow2D GPU probe or game performance tests. Continue
those checks on a physical Mac with the user's ROM and keep ROMs and ROM-derived
captures out of CI artifacts.

## Reproduce the development probe

Install Xcode Command Line Tools (or Xcode) and Homebrew, then:

```sh
brew install cmake ninja sdl2 glew spdlog nlohmann-json tinyxml2 libzip \
  vulkan-headers vulkan-loader molten-vk shaderc
scripts/macos/build.sh
```

This compiles the generic renderer and input/audio service tests, opens a
short-lived Vulkan window, and checks rendered pixels. It needs no ROM.
Results are written under `build-macos/`:

- `macos-shadow2d-probe.json`: GPU readback assertions.
- `macos-shadow2d-probe.bmp`: the captured image.
- `oot3d_shadow2d_backend_probe`: native executable.

The script uses the current macOS deployment version because Homebrew bottles
may require that version. `MACOSX_DEPLOYMENT_TARGET` can override it when using
dependencies built for an older OS. Intel and universal builds, minimum OS
compatibility and distribution signing have not been verified. The local app
bundle has been verified with ad-hoc signing.

NRI, CACAO and SSSR are disabled for this first development path. Vulkan runs
through MoltenVK over Metal; this does not establish parity for all game
shaders or Windows graphics enhancements. The inherited Fast3D Metal backend
is compiled but is not the backend qualified by this probe.

## Changes and evidence (2026-09-09)

- Enable `VK_KHR_portability_enumeration` and its instance flag when advertised,
  and enable `VK_KHR_portability_subset` on devices that advertise it. These are
  required for [MoltenVK discovery and device creation](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md).
- Replace invalid `std::make_unsigned` specializations in the retained nihstro
  code with a local trait, fixing compilation with AppleClang/libc++.
- Qualify the Metal shader's PICA constant with its `Fast` namespace.
- Link the independent Shadow2D probe to the existing small application host,
  so it can build without the development cutscene sources.
- Ignore the local `rom/` directory in Git.

Verified with AppleClang 21 on arm64 macOS 26.6.2: the generic renderer static library and
Shadow2D executable build; input and audio service tests exit successfully.
The Vulkan probe returns zero, with `visual_sample_compare_passed=true` and
`consecutive_framebuffers_stable=true` across RGB and RGBA shader variants.
The shadow center luminance is 0 and the lit corner luminance is 1.
This tests rendering and readback, not gameplay or actual audio playback.

The user-supplied USA cartridge also extracts successfully with
`tools/triaevum_release/ctr_rom.py`. Its code SHA-256 is
`ef210566e1d9d16879a746dfb063fcbad232f0171d860de906531ecc526cc020`,
matching the existing USA adapter's input code identity. Content adaptation,
frozen Forge import, ABI-v2 plugin loading, title intro, file selection and
interactive gameplay have now been exercised.

## UI dependency recovered

The original main branch and alpha.1c archives omit `oot3d_ui/`, but upstream
published the complete dependency in commit
[`c5d85340a8f8b8d48538c49d8933cbb9e3d3c0ac`](https://github.com/coccofresco/TriAevum/commit/c5d85340a8f8b8d48538c49d8933cbb9e3d3c0ac)
on `port/linux-nri`. This fork now incorporates that commit's 62 UI files,
manifest, CMake integration, source-package checks and provenance notice.
The old evidence-directory dependency and silent empty target are removed.

The importer describes the files as generated from reviewed code bodies,
but `import_zelda3drecomp_evidence.py` only copies committed files from the
separate Zelda3drecomp checkout; it does not generate them from a ROM.
The published module explicitly requires no regeneration. See
[its README](../tools/oot3d/ui_contract/README.md) and
[upstream's repair explanation](TRIAEVUM_UI_SOURCE_REPAIR.md).

On this Mac, the complete UI contract builds and all three existing standalone
tests pass (contract, gameplay HUD and TopScreen item hints):

```sh
cmake -S tools/oot3d/ui_contract -B build-macos-ui -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-macos-ui --parallel 6
ctest --test-dir build-macos-ui --output-on-failure
```

The three UI tests and 20 source-archive/release-audit tests pass; all 62
imported source hashes match the upstream manifest. Also adopted upstream
`port/linux-nri`'s conditional link to the development-only `oot3d_native_math`
target, which is absent from public snapshots. The complete generic host now
links at `build-macos/TriAevum` as arm64 Mach-O. `--product-info` exits zero,
reports ABI 2 and TopScreen/F1 support, and correctly reports
`private_title_loaded=false` for the empty stub and `true` when the compiled
title dylib is selected with `--verify-title-plugin`.

## Playable title and application

The Windows release includes translated C++ in its nested `source/titles/`
archive. `tools/triaevum_release/macos_title/CMakeLists.txt` verifies its source
manifest and compiles it into the native arm64 ABI-v2 title dylib. The host uses
`dlopen`/`dlsym` and executable-relative discovery on macOS.

`tools/triaevum_release/macos_forge.py` shares Forge's ROM verification,
USA-to-canonical adaptation and TopScreen asset import. The frozen importer
is launched by `ports/macos/Launcher.swift`. User settings and saves live in
`~/Library/Application Support/TriAevum`, outside the application bundle.
`tools/triaevum_release/package_macos.py` bundles and relocates dylibs, writes
the MoltenVK ICD and signs the local app ad-hoc.

`scripts/macos/build.sh runtime` rebuilds only the runtime;
`scripts/macos/build-app.sh` orchestrates the full application build. Clean-machine validation,
Intel/universal builds, notarization and a complete game playthrough remain.

## Retina performance

The framebuffer resize check previously compared logical window points with
physical swapchain pixels. On Retina displays this invalidated the swapchain
every frame, destroying pipelines and the ImGui font atlas. The check now
compares consecutive requested sizes; real window events and Vulkan
`OUT_OF_DATE` still trigger recreation. Readback also resamples the full
drawable instead of cropping its upper-left corner.

Both compiled shaders and the Vulkan pipeline cache persist under
`~/Library/Application Support/oot3d_native_vulkan/shader_cache`. The cache
cannot prevent the overhead of repeatedly destroying live pipelines.

On this Mac, the same fixed-step 900-frame opening sequence improved from
18.3 FPS (49.13 seconds) to 47.6 FPS (18.91 seconds) in the rebuilt app.
A separate 1,800-frame run measured 57.8 FPS over its final 900 frames after
warm-up. Tests retained the original render scale, toon effect and dense grass,
with VSync and the SDL frame limiter enabled. These are opening-sequence
measurements, not a full-game performance guarantee. The Retina-aware
Shadow2D RGB/RGBA readback probe also passes with stable consecutive frames.

## Real-time pacing and grass admission

Interactive pacing now uses the tested two-period recovery bound instead of
retaining up to 250 ms of presentation debt. Long delays rebase the presentation
deadline; the native simulation still follows elapsed time at 30 Hz.

On macOS the grass pass polls the existing worker cache instead of waiting for
all placements before admitting the scene. Pending surfaces appear when their
immutable placement data is ready. This preserves grass density and rendering
quality, with initial grass pop-in in exchange for avoiding a multi-second
main-thread stall. The synchronous cache API remains the default for other
callers and deterministic checks.

In 45-second real-time opening runs with the same graphics settings, these
changes reduced the maximum presentation interval from 7.016 seconds to
99.6 ms, and RMS interval error from 150.6 ms to 3.7 ms. The final run averaged
55.0 presentation FPS and advanced 1,337 game simulation frames (29.7 Hz).
Small hitches remain. The frame-rate/scheduler checks and all 12 grass cache
tests pass, including nonblocking admission followed by completed-result reuse.

ROM inputs and extracted files stay under ignored `rom/` and `build-macos/`.
No ROM or extracted game assets should be committed.
