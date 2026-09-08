# Controller release audit

Date: 2026-09-08. Scope: the published Windows `v0.6.0-alpha.1` runtime,
native device routing, and public package dependencies. No ROM adaptation,
renderer behavior or title logic is changed by this repair.

Follow-up: the maintainer connected a physical controller, tested the patched
game from startup and confirmed that it works. Fix commit: `d88ebb5`. This
confirmation does not establish compatibility with every controller model.

## Confirmed defects

1. `oot3d_demo_host_context.cpp` initialized video and audio but only scanned
   gamepads. `SDL_INIT_GAMECONTROLLER` and external mapping loading lived in
   the legacy N64 `osContInit`, which the native game does not call.
2. `Oot3dNativeGameLaunch::ControlConfig` selected the keyboard/mouse-only
   preset. It disabled gamepad buttons and the movement stick on fresh installs.
3. The published allowlist did not include `gamecontrollerdb.txt`. SDL's
   built-in mappings cannot cover every controller recognized by the community DB.
4. Device rescans cleared the map and reopened handles without closing them.
5. `shaderc_shared.dll` imports `msvcp140.dll`, `vcruntime140.dll` and
   `vcruntime140_1.dll`. The published package omitted these files and depended
   on a separately installed Visual C++ runtime. This is unrelated to SDL input.

The inspected published `TriAevum.exe` SHA-256 is
`dd80b05e6c3b2129d3023150f00cf4fadfbd3ff546ebf481b74f5819af3dba36`.
Its PE imports and those of the stub, title DLL and neutral Forge module contain
no `SDL2.dll` dependency. SDL2 is statically linked, not missing as a DLL.
The reporter's exact message and controller model have not yet been supplied;
do not assume a different executable or a mapping warning means a missing DLL.

## Repair boundaries

- `ConnectedPhysicalDeviceManager::Initialize/Shutdown` own one SDL subsystem
  reference, optional mapping loading, handles and hotplug cleanup. The native
  host initializes it after SDL video and shuts it down before window teardown.
  The legacy entry point delegates to the same owner, avoiding duplicate init.
- Rescans preserve attached handles and port ignores, close removed handles,
  and remove stale instance IDs. No per-frame controller reopening is added.
- `NativeControlDefaults()` retains keyboard/mouse bindings while enabling
  controller buttons, left-stick movement and automatic motion-source selection.
  Existing user files and explicit keyboard-only presets remain authoritative.
- The release layout requires `resources/gamecontrollerdb.txt`, SDL notices,
  and shaderc's three Visual C++ DLLs. The publisher supplies the latter via
  `--vc-redist-dir`; Forge users do not need an SDK or compiler.
- The community DB is pinned to
  `28a856f2b92da8891b161acd0abd64fbf4445d97`; provenance and hash are recorded in
  `THIRD_PARTY_NOTICES.md`. Missing optional mappings warn but do not disable
  SDL's built-in mappings. A public package missing the DB fails its audit.

## Verification

- `runtime/three_ds_recomp/tests/controller` builds the actual device manager
  against SDL2, without a renderer or ROM. SDL virtual controllers exercise
  initialization without the legacy path, A/Start press and release, both sticks,
  100 rescans, port ignores, unplug/reconnect, preconnected devices, idempotent
  initialization, external SDL ownership and teardown after `SDL_Quit`.
- SDL2 2.32.10 accepted 583 additional Windows mappings. No physical controller
  was connected on this machine during this test. Virtual tests do not certify
  Bluetooth drivers, rumble, hardware motion sensors, or the reporter's device.
- `oot3d_native_a32_input_tests` passes, including combined default movement,
  native camera axes, config round trips and existing input/Start-routing tests.
- Release audit and precompiled-release tests plus `test_runtime_redist` pass.
  Omitted mappings or any of the three C++ DLLs are rejected even when they
  happen to be installed on the publisher's machine.
  Full Forge/release discovery ran 187 tests: 185 passed and two optional
  environment-dependent checks were skipped initially. Both checks then passed
  after supplying `CMAKE_COMMAND` and `TRIAEVUM_PRODUCT_TEST_EXE` for this build.
- A bounded NRI boot of the patched runtime exited normally after 25 seconds,
  with 463 game updates / 960 presentations and framebuffer title captures.
  This is an integration smoke test, not a controller gameplay or FPS benchmark.

Private evidence: `I:/oot3dre_work/triaevum-controller-check/boot`.
Candidate binary SHA-256:
`29141ce1775e108a8585c5733dfe1ae59f61534edc1539bf045d9f7978919d40`.
It was built incrementally using the existing runtime cache and the edited
native sources copied after verifying their baseline blob identities. No title
AOT rebuild was performed. Its embedded source receipt still identifies the
cached baseline `7875f7d14eb0cb09adbed8915098aab431f71323`; it is a local test
binary, **not a newly qualified release**. Publish only through the normal clean
release workflow with regenerated source/catalog/runtime receipts. The published
GitHub release has not been replaced by this audit.

## Repeat the tests

Configure `runtime/three_ds_recomp/tests/controller` as an independent CMake
project with SDL2 >= 2.24 and spdlog packages, then build and run CTest.
On this machine the static prefix is `C:/vcpkg/installed/x64-windows-static`
and the test build is `I:/oot3dre_work/triaevum-controller-tests`.
The standalone CMake target handles the SDL2/Clang 22 intrinsic-header conflict.
The existing runtime build used the equivalent `/FIintrin.h` compiler option.
Use the existing MSVC 14.44.35207 includes/libraries, Windows SDK 10.0.19041.0
including `winrt`, and LLVM 22.1.6; direct temporary output to drive I.

Run `python -m unittest test_release_audit test_precompiled_release
test_runtime_redist` from `tools/triaevum_release` with the repository and tool
directory on `PYTHONPATH`. The separate native input target is
`oot3d_native_a32_input_tests`.

For an existing installation, inspect F1 > Controls: enable the controller or
select its preset, and check that the selected device/GUID is available. Do not
erase a user's bindings to apply new defaults. A missing `SDL2.dll` message from
another build requires that executable's identity and full error text; never
recommend arbitrary DLL-download sites.
